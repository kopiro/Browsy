/*
 * MacTCP.h - TCP Manager Interfaces
 * Based on Apple Universal Interfaces 2.1b1, adapted for Retro68 multiversal.
 */
#ifndef __MACTCP__
#define __MACTCP__

#include <MacTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MacTCP error codes */
enum {
    inProgress              = 1,
    ipBadLapErr             = -23000,
    ipBadCnfgErr            = -23001,
    ipNoCnfgErr             = -23002,
    ipLoadErr               = -23003,
    ipBadAddr               = -23004,
    connectionClosing       = -23005,
    invalidLength           = -23006,
    connectionExists        = -23007,
    connectionDoesntExist   = -23008,
    insufficientResources   = -23009,
    invalidStreamPtr        = -23010,
    streamAlreadyOpen       = -23011,
    connectionTerminated    = -23012,
    invalidBufPtr           = -23013,
    invalidRDS              = -23014,
    invalidWDS              = -23014,
    openFailed              = -23015,
    commandTimeout          = -23016,
    duplicateSocket         = -23017,
    ipDontFragErr           = -23032,
    ipDestDeadErr           = -23033,
    icmpEchoTimeoutErr      = -23035,
    ipNoFragMemErr          = -23036,
    ipRouteErr              = -23037,
    nameSyntaxErr           = -23041,
    cacheFault              = -23042,
    noResultProc            = -23043,
    noNameServer            = -23044,
    authNameErr             = -23045,
    noAnsErr                = -23046,
    dnrErr                  = -23047,
    outOfMemory             = -23048
};

typedef unsigned long  ip_addr;
typedef unsigned short ip_port;
typedef unsigned long  b_16;
typedef unsigned long  b_32;
typedef unsigned long  BufferPtr;
typedef unsigned long  StreamPtr;
typedef unsigned short tcp_port;
typedef unsigned short udp_port;
typedef unsigned short ICMPMsgType;
typedef unsigned short TCPEventCode;
typedef unsigned short TCPTerminationReason;
typedef unsigned short UDPEventCode;

struct ip_addrbytes {
    union {
        b_32           addr;
        unsigned char  byte[4];
    } a;
};
typedef struct ip_addrbytes ip_addrbytes;

struct wdsEntry {
    unsigned short length;
    Ptr            ptr;
};
typedef struct wdsEntry wdsEntry;

struct rdsEntry {
    unsigned short length;
    Ptr            ptr;
};
typedef struct rdsEntry rdsEntry;

struct ICMPReport {
    StreamPtr      streamPtr;
    ip_addr        localHost;
    ip_port        localPort;
    ip_addr        remoteHost;
    ip_port        remotePort;
    short          reportType;
    unsigned short optionalAddlInfo;
    unsigned long  optionalAddlInfoPtr;
};
typedef struct ICMPReport ICMPReport;

/* TCP event codes */
enum {
    TCPClosing      = 1,
    TCPULPTimeout   = 2,
    TCPTerminate    = 3,
    TCPDataArrival  = 4,
    TCPUrgent       = 5,
    TCPICMPReceived = 6,
    lastEvent       = 32767
};

/* TCP csCode values */
enum {
    TCPCreate       = 30,
    TCPPassiveOpen  = 31,
    TCPActiveOpen   = 32,
    TCPSend         = 34,
    TCPNoCopyRcv    = 35,
    TCPRcvBfrReturn = 36,
    TCPRcv          = 37,
    TCPClose        = 38,
    TCPAbort        = 39,
    TCPStatus       = 40,
    TCPExtendedStat = 41,
    TCPRelease      = 42,
    TCPGlobalInfo   = 43,
    TCPCtlMax       = 49
};

/* Validity flags for open */
enum {
    timeoutValue  = 0x80,
    timeoutAction = 0x40,
    typeOfService = 0x20,
    precedence    = 0x10
};

typedef pascal void (*TCPNotifyProcPtr)(
    StreamPtr tcpStream, unsigned short eventCode,
    Ptr userDataPtr, unsigned short terminReason,
    struct ICMPReport *icmpMsg);
typedef TCPNotifyProcPtr TCPNotifyUPP;

struct TCPCreatePB {
    Ptr            rcvBuff;
    unsigned long  rcvBuffLen;
    TCPNotifyUPP   notifyProc;
    Ptr            userDataPtr;
};
typedef struct TCPCreatePB TCPCreatePB;

struct TCPOpenPB {
    SInt8          ulpTimeoutValue;
    SInt8          ulpTimeoutAction;
    SInt8          validityFlags;
    SInt8          commandTimeoutValue;
    ip_addr        remoteHost;
    tcp_port       remotePort;
    ip_addr        localHost;
    tcp_port       localPort;
    SInt8          tosFlags;
    SInt8          precedence;
    Boolean        dontFrag;
    SInt8          timeToLive;
    SInt8          security;
    SInt8          optionCnt;
    SInt8          options[40];
    Ptr            userDataPtr;
};
typedef struct TCPOpenPB TCPOpenPB;

struct TCPSendPB {
    SInt8          ulpTimeoutValue;
    SInt8          ulpTimeoutAction;
    SInt8          validityFlags;
    Boolean        pushFlag;
    Boolean        urgentFlag;
    SInt8          filler;
    Ptr            wdsPtr;
    unsigned long  sendFree;
    unsigned short sendLength;
    Ptr            userDataPtr;
};
typedef struct TCPSendPB TCPSendPB;

struct TCPReceivePB {
    SInt8          commandTimeoutValue;
    Boolean        markFlag;
    Boolean        urgentFlag;
    SInt8          filler;
    Ptr            rcvBuff;
    unsigned short rcvBuffLen;
    Ptr            rdsPtr;
    unsigned short rdsLength;
    unsigned short secondTimeStamp;
    Ptr            userDataPtr;
};
typedef struct TCPReceivePB TCPReceivePB;

struct TCPClosePB {
    SInt8          ulpTimeoutValue;
    SInt8          ulpTimeoutAction;
    SInt8          validityFlags;
    SInt8          filler;
    Ptr            userDataPtr;
};
typedef struct TCPClosePB TCPClosePB;

struct TCPAbortPB {
    Ptr userDataPtr;
};
typedef struct TCPAbortPB TCPAbortPB;

struct TCPStatusPB {
    SInt8          ulpTimeoutValue;
    SInt8          ulpTimeoutAction;
    long           unused;
    ip_addr        remoteHost;
    tcp_port       remotePort;
    ip_addr        localHost;
    tcp_port       localPort;
    SInt8          tosFlags;
    SInt8          precedence;
    SInt8          connectionState;
    SInt8          filler;
    unsigned short sendWindow;
    unsigned short rcvWindow;
    unsigned short amtUnackedData;
    unsigned short amtUnreadData;
    Ptr            securityLevelPtr;
    unsigned long  sendUnacked;
    unsigned long  sendNext;
    unsigned long  congestionWindow;
    unsigned long  rcvNext;
    unsigned long  srtt;
    unsigned long  lastRTT;
    unsigned long  sendMaxSegSize;
    void           *connStatPtr;
    Ptr            userDataPtr;
};
typedef struct TCPStatusPB TCPStatusPB;

typedef StreamPtr *StreamPPtr;
struct TCPGlobalInfoPB {
    void           *tcpParamPtr;
    void           *tcpStatsPtr;
    StreamPPtr     tcpCDBTable[1];
    Ptr            userDataPtr;
    unsigned short maxTCPConnections;
};
typedef struct TCPGlobalInfoPB TCPGlobalInfoPB;

typedef struct TCPiopb TCPiopb;
typedef void (*TCPIOCompletionProcPtr)(TCPiopb *iopb);
typedef TCPIOCompletionProcPtr TCPIOCompletionUPP;

/*
 * TCPiopb -- the TCP I/O parameter block passed to PBControl.
 *
 * fill12[12] covers the standard ParamBlockRec queue header:
 *   qLink (4) + qType (2) + ioTrap (2) + ioCmdAddr (4) = 12 bytes
 * You never set these -- PBControl fills them internally.
 */
/* TCPiopb was forward-declared above for the completion proc typedef */
struct TCPiopb {
    SInt8              fill12[12];
    TCPIOCompletionUPP ioCompletion;
    short              ioResult;
    Ptr                ioNamePtr;
    short              ioVRefNum;
    short              ioCRefNum;
    short              csCode;
    StreamPtr          tcpStream;
    union {
        TCPCreatePB     create;
        TCPOpenPB       open;
        TCPSendPB       send;
        TCPReceivePB    receive;
        TCPClosePB      close;
        TCPAbortPB      abort;
        TCPStatusPB     status;
        TCPGlobalInfoPB globalInfo;
    } csParam;
};

/* GetAddrParamBlock for ipctlGetAddr */
enum { ipctlGetAddr = 15 };

struct GetAddrParamBlock {
    QElemPtr     qLink;
    short        qType;
    short        ioTrap;
    Ptr          ioCmdAddr;
    ProcPtr      ioCompletion;
    OSErr        ioResult;
    StringPtr    ioNamePtr;
    short        ioVRefNum;
    short        ioCRefNum;
    short        csCode;
    ip_addr      ourAddress;
    long         ourNetMask;
};
typedef struct GetAddrParamBlock GetAddrParamBlock;

#ifdef __cplusplus
}
#endif

#endif /* __MACTCP__ */

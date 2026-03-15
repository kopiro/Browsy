/*
 * AddressXlation.h - MacTCP DNR interface
 * Based on Apple Universal Interfaces, adapted for Retro68 multiversal.
 */
#ifndef __ADDRESSXLATION__
#define __ADDRESSXLATION__

#include "MacTCP.h"

#define NUM_ALT_ADDRS 4

struct hostInfo {
    /*
     * MacTCP's DNR result layout expects a 2-byte leading pad before the
     * 2-byte rtnCode. Without this, result fields land at the wrong offsets.
     */
    short   _pad;
    OSErr   rtnCode;
    char    cname[255];
    ip_addr addr[NUM_ALT_ADDRS];
};

struct returnRec {
    OSErr   rtnCode;
    char    cname[255];
    /* additional fields for HINFO/MX records */
    short   preference;
    char    exchange[255];
};

struct cacheEntryRecord {
    char    *cname;
    short   type;
    short   cacheClass;
    long    ttl;
    union {
        char    *name;
        ip_addr addr;
    } rdata;
};

/* DNR selector codes */
enum {
    OPENRESOLVER  = 1,
    CLOSERESOLVER = 2,
    STRTOADDR     = 3,
    ADDRTOSTR     = 4,
    ENUMCACHE     = 5,
    ADDRTONAME    = 6,
    HXINFO        = 7,
    MXINFO        = 8
};

/* Result callback types -- on 68k these are plain function pointers */
typedef pascal void (*ResultProcPtr)(struct hostInfo *hostInfoPtr, char *userDataPtr);
typedef ResultProcPtr ResultUPP;

typedef pascal void (*ResultProc2ProcPtr)(struct returnRec *returnRecPtr, char *userDataPtr);
typedef ResultProc2ProcPtr ResultProc2UPP;

typedef pascal void (*EnumResultProcPtr)(struct cacheEntryRecord *entryPtr, char *userDataPtr);
typedef EnumResultProcPtr EnumResultUPP;

/*
 * The DNR resource is entered as a normal code resource function with the
 * selector passed as the first argument. Give each call its real signature
 * so the compiler lays out arguments correctly on 68k.
 */
typedef OSErr (*OpenResolverProcPtr)(UInt32 selector, char *fileName);
typedef OSErr (*CloseResolverProcPtr)(UInt32 selector);
typedef OSErr (*StrToAddrProcPtr)(UInt32 selector, char *hostName,
                                  struct hostInfo *hostInfoPtr,
                                  ResultUPP resultProc, Ptr userDataPtr);
typedef OSErr (*AddrToStrProcPtr)(UInt32 selector, unsigned long addr,
                                  char *addrStr);
typedef OSErr (*EnumCacheProcPtr)(UInt32 selector, EnumResultUPP resultProc,
                                  Ptr userDataPtr);
typedef OSErr (*AddrToNameProcPtr)(UInt32 selector, ip_addr addr,
                                   struct hostInfo *hostInfoPtr,
                                   ResultUPP resultProc, Ptr userDataPtr);
typedef OSErr (*HInfoProcPtr)(UInt32 selector, char *hostName,
                              struct returnRec *returnRecPtr,
                              ResultProc2UPP resultProc, Ptr userDataPtr);
typedef OSErr (*MXInfoProcPtr)(UInt32 selector, char *hostName,
                               struct returnRec *returnRecPtr,
                               ResultProc2UPP resultProc, Ptr userDataPtr);

#define CallOpenResolverProc(proc, sel, fileName) \
    ((OpenResolverProcPtr)(proc))((sel), (fileName))
#define CallCloseResolverProc(proc, sel) \
    ((CloseResolverProcPtr)(proc))((sel))
#define CallStrToAddrProc(proc, sel, hostName, hostInfoPtr, resultProc, userDataPtr) \
    ((StrToAddrProcPtr)(proc))((sel), (hostName), (hostInfoPtr), (resultProc), (userDataPtr))
#define CallAddrToStrProc(proc, sel, addr, addrStr) \
    ((AddrToStrProcPtr)(proc))((sel), (addr), (addrStr))
#define CallEnumCacheProc(proc, sel, resultProc, userDataPtr) \
    ((EnumCacheProcPtr)(proc))((sel), (resultProc), (userDataPtr))
#define CallAddrToNameProc(proc, sel, addr, hostInfoPtr, resultProc, userDataPtr) \
    ((AddrToNameProcPtr)(proc))((sel), (addr), (hostInfoPtr), (resultProc), (userDataPtr))

/* Public API */
OSErr OpenResolver(char *fileName);
OSErr CloseResolver(void);
OSErr StrToAddr(char *hostName, struct hostInfo *hostInfoPtr,
                ResultUPP resultproc, Ptr userDataPtr);
OSErr AddrToStr(unsigned long addr, char *addrStr);
OSErr EnumCache(EnumResultUPP resultproc, Ptr userDataPtr);
OSErr AddrToName(unsigned long addr, struct hostInfo *hostInfoPtr,
                 ResultUPP resultproc, Ptr userDataPtr);

#endif /* __ADDRESSXLATION__ */

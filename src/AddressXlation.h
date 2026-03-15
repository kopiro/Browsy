/*
 * AddressXlation.h - MacTCP DNR interface
 * Based on Apple Universal Interfaces, adapted for Retro68 multiversal.
 */
#ifndef __ADDRESSXLATION__
#define __ADDRESSXLATION__

#include "MacTCP.h"

#define NUM_ALT_ADDRS 4

struct hostInfo {
    long    rtnCode;
    char    cname[255];
    ip_addr addr[NUM_ALT_ADDRS];
};

struct returnRec {
    long    rtnCode;
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
 * DNR dispatch macros -- for 68k (non-CFM), these call through the
 * code pointer loaded from the 'dnrp' resource.
 */
typedef OSErr (*DNRProcPtr)();

#define CallOpenResolverProc(proc, sel, fn) \
    (*(DNRProcPtr)(proc))((sel), (fn))

#define CallCloseResolverProc(proc, sel) \
    (*(DNRProcPtr)(proc))((sel))

#define CallStrToAddrProc(proc, sel, host, rtn, cb, ud) \
    (*(DNRProcPtr)(proc))((sel), (host), (rtn), (cb), (ud))

#define CallAddrToStrProc(proc, sel, addr, str) \
    (*(DNRProcPtr)(proc))((sel), (addr), (str))

#define CallEnumCacheProc(proc, sel, cb, ud) \
    (*(DNRProcPtr)(proc))((sel), (cb), (ud))

#define CallAddrToNameProc(proc, sel, addr, rtn, cb, ud) \
    (*(DNRProcPtr)(proc))((sel), (addr), (rtn), (cb), (ud))

#define CallHInfoProc(proc, sel, host, rtn, cb, ud) \
    (*(DNRProcPtr)(proc))((sel), (host), (rtn), (cb), (ud))

#define CallMXInfoProc(proc, sel, host, rtn, cb, ud) \
    (*(DNRProcPtr)(proc))((sel), (host), (rtn), (cb), (ud))

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

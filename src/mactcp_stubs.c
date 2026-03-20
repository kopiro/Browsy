/*
 * dnr.c - MacTCP Domain Name Resolver glue code
 * Based on Apple's DNR library (c) 1989-1995 Apple Computer, Inc.
 * Loads the 'dnrp' code resource from the MacTCP driver file.
 */
#include <OSUtils.h>
#include <Errors.h>
#include <Files.h>
#include <Resources.h>
#include <Memory.h>
#include <Traps.h>
#include <Gestalt.h>
#include <ToolUtils.h>
#include <string.h>
#include "MacTCP.h"
#include "AddressXlation.h"
#include "utils.h"

/* Folders.h redirect -- FindFolder and friends are in Multiverse.h */

#ifndef _GestaltDispatch
#define _GestaltDispatch _Gestalt
#endif

static Handle  gDNRCodeHndl = nil;
static ProcPtr gDNRCodePtr  = nil;
static Str255  gMacTCPFileName = "\006MacTCP";
typedef short ResolverTrapType;

enum {
    kResolverOSTrap = 0,
    kResolverToolTrap = 1
};

static void ResolverStageAlert(char *stage, OSErr err)
{
    alertf("DNR %s %hd", stage, err);
}

static short NumToolboxTraps(void)
{
    if (GetTrapAddress(_InitGraf) == GetTrapAddress(0xAA6E))
        return 0x200;
    return 0x400;
}

static ResolverTrapType GetTrapType(short theTrap)
{
    if ((theTrap & 0x0800) == 0)
        return kResolverOSTrap;
    return kResolverToolTrap;
}

static Boolean TrapExists(short theTrap)
{
    ResolverTrapType theTrapType;

    theTrapType = GetTrapType(theTrap);
    if ((theTrapType == kResolverToolTrap) &&
        ((theTrap &= 0x07FF) >= NumToolboxTraps()))
        theTrap = _Unimplemented;

    return NGetTrapAddress(_Unimplemented, kResolverToolTrap) !=
           NGetTrapAddress(theTrap, theTrapType);
}

static OSErr PBHGetFInfoCompat(HParmBlkPtr filePB)
{
    return PBHGetFInfoSync(filePB);
}

static short TryOpenDNRPByName(short vRefNum, long dirID, StringPtr fileName)
{
    WDPBRec wdPB;
    short refnum;
    short wdRefNum;

    memset(&wdPB, 0, sizeof(wdPB));
    wdPB.ioVRefNum = vRefNum;
    wdPB.ioWDDirID = dirID;
    wdPB.ioWDProcID = 'BWsy';
    if (PBOpenWDSync(&wdPB) != noErr)
        return -1;

    wdRefNum = wdPB.ioVRefNum;
    refnum = OpenRFPerm(fileName, wdRefNum, fsRdPerm);
    memset(&wdPB, 0, sizeof(wdPB));
    wdPB.ioVRefNum = wdRefNum;
    PBCloseWDSync(&wdPB);
    if (refnum == -1)
        return -1;
    if (GetIndResource('dnrp', 1) == NULL) {
        CloseResFile(refnum);
        return -1;
    }
    return refnum;
}

static void GetSystemFolder(short *vRefNumP, long *dirIDP)
{
    SysEnvRec info;
    long wdProcID;

    SysEnvirons(1, &info);
    if (GetWDInfo(info.sysVRefNum, vRefNumP, dirIDP, &wdProcID) != noErr) {
        *vRefNumP = 0;
        *dirIDP = 0;
    }
}

static void GetCPanelFolder(short *vRefNumP, long *dirIDP)
{
    Boolean hasFolderMgr = false;
    long feature;

    if (TrapExists(_GestaltDispatch) &&
        Gestalt(gestaltFindFolderAttr, &feature) == noErr)
        hasFolderMgr = true;

    if (!hasFolderMgr) {
        GetSystemFolder(vRefNumP, dirIDP);
        return;
    }

    if (FindFolder(kOnSystemDisk, kControlPanelFolderType,
                   kDontCreateFolder, vRefNumP, dirIDP) != noErr) {
        *vRefNumP = 0;
        *dirIDP = 0;
    }
}

/* Search a folder for files containing the 'dnrp' resource */
static short SearchFolderForDNRP(long targetType, long targetCreator,
                                 short vRefNum, long dirID)
{
    HParamBlockRec fi;
    Str255 filename;
    short refnum;

    fi.fileParam.ioCompletion = nil;
    fi.fileParam.ioNamePtr = filename;
    fi.fileParam.ioVRefNum = vRefNum;
    fi.fileParam.ioDirID = dirID;
    fi.fileParam.ioFDirIndex = 1;

    while (PBHGetFInfoCompat((HParmBlkPtr)&fi) == noErr) {
        if (fi.fileParam.ioFlFndrInfo.fdType == targetType &&
            fi.fileParam.ioFlFndrInfo.fdCreator == targetCreator) {
            refnum = HOpenResFile(vRefNum, dirID, filename, fsRdPerm);
            if (GetIndResource('dnrp', 1) == NULL)
                CloseResFile(refnum);
            else
                return refnum;
        }
        fi.fileParam.ioFDirIndex++;
        fi.fileParam.ioDirID = dirID;
    }
    return -1;
}

/* Open the MacTCP driver's resource fork */
static short OpenOurRF(void)
{
    short refnum;
    short vRefNum;
    long dirID;

    /* On System 6, the System Folder path is the safest first probe. */
    ResolverStageAlert("rf-sys-ztcp", noErr);
    GetSystemFolder(&vRefNum, &dirID);
    ResolverStageAlert("rf-sysdir", noErr);
    ResolverStageAlert("rf-sys-name", noErr);
    refnum = TryOpenDNRPByName(vRefNum, dirID, gMacTCPFileName);
    if (refnum != -1) return refnum;
    ResolverStageAlert("rf-sys-scan", noErr);
    refnum = SearchFolderForDNRP('cdev', 'ztcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    ResolverStageAlert("rf-sys-mtcp", noErr);
    refnum = TryOpenDNRPByName(vRefNum, dirID, gMacTCPFileName);
    if (refnum != -1) return refnum;
    ResolverStageAlert("rf-sys-scan2", noErr);
    refnum = SearchFolderForDNRP('cdev', 'mtcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    /* Keep the older Control Panels fallback for newer layouts. */
    ResolverStageAlert("rf-cpanel-ztcp", noErr);
    GetCPanelFolder(&vRefNum, &dirID);
    ResolverStageAlert("rf-cpdir", noErr);
    ResolverStageAlert("rf-cp-name", noErr);
    refnum = TryOpenDNRPByName(vRefNum, dirID, gMacTCPFileName);
    if (refnum != -1) return refnum;
    ResolverStageAlert("rf-cp-scan", noErr);
    refnum = SearchFolderForDNRP('cdev', 'ztcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    ResolverStageAlert("rf-cpanel-mtcp", noErr);
    GetCPanelFolder(&vRefNum, &dirID);
    refnum = TryOpenDNRPByName(vRefNum, dirID, gMacTCPFileName);
    if (refnum != -1) return refnum;
    ResolverStageAlert("rf-cp-scan2", noErr);
    refnum = SearchFolderForDNRP('cdev', 'mtcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    return -1;
}

OSErr OpenResolver(char *fileName)
{
    short refnum;
    OSErr rc;

    if (gDNRCodePtr != nil)
        return noErr;

    ResolverStageAlert("load-direct", noErr);
    gDNRCodeHndl = GetIndResource('dnrp', 1);
    if (gDNRCodeHndl != nil)
        goto got_resource;

    ResolverStageAlert("open-rf", noErr);
    refnum = OpenOurRF();
    if (refnum == -1)
        ResolverStageAlert("open-rf", fnfErr);

    ResolverStageAlert("load-rsrc", noErr);
    gDNRCodeHndl = GetIndResource('dnrp', 1);
    if (gDNRCodeHndl == nil) {
        ResolverStageAlert("load-rsrc", ResError());
        return ResError();
    }

got_resource:
    ReserveMem(16000L);
    DetachResource(gDNRCodeHndl);
    if (refnum != -1)
        CloseResFile(refnum);

    ResolverStageAlert("lock-rsrc", noErr);
    MoveHHi(gDNRCodeHndl);
    HLock(gDNRCodeHndl);
    gDNRCodePtr = (ProcPtr)*gDNRCodeHndl;

    ResolverStageAlert("open-call", noErr);
    rc = CallOpenResolverProc(gDNRCodePtr, OPENRESOLVER, fileName);
    if (rc != noErr) {
        ResolverStageAlert("open-call", rc);
        HUnlock(gDNRCodeHndl);
        DisposeHandle(gDNRCodeHndl);
        gDNRCodePtr = nil;
    }
    return rc;
}

OSErr CloseResolver(void)
{
    if (gDNRCodePtr == nil)
        return notOpenErr;

    CallCloseResolverProc(gDNRCodePtr, CLOSERESOLVER);

    HUnlock(gDNRCodeHndl);
    DisposeHandle(gDNRCodeHndl);
    gDNRCodePtr = nil;
    return noErr;
}

OSErr StrToAddr(char *hostName, struct hostInfo *rtnStruct,
                ResultUPP resultproc, Ptr userDataPtr)
{
    OSErr rc;

    if (gDNRCodePtr == nil)
        return notOpenErr;

    ResolverStageAlert("strtoaddr", noErr);
    rc = CallStrToAddrProc(gDNRCodePtr, STRTOADDR, hostName,
                           rtnStruct, resultproc, userDataPtr);
    if (rc != noErr && rc != cacheFault)
        ResolverStageAlert("strtoaddr", rc);
    return rc;
}

OSErr AddrToStr(unsigned long addr, char *addrStr)
{
    if (gDNRCodePtr == nil)
        return notOpenErr;

    CallAddrToStrProc(gDNRCodePtr, ADDRTOSTR, addr, addrStr);
    return noErr;
}

OSErr EnumCache(EnumResultUPP resultproc, Ptr userDataPtr)
{
    if (gDNRCodePtr == nil)
        return notOpenErr;

    return CallEnumCacheProc(gDNRCodePtr, ENUMCACHE, resultproc,
                             userDataPtr);
}

OSErr AddrToName(unsigned long addr, struct hostInfo *rtnStruct,
                 ResultUPP resultproc, Ptr userDataPtr)
{
    if (gDNRCodePtr == nil)
        return notOpenErr;

    return CallAddrToNameProc(gDNRCodePtr, ADDRTONAME, addr,
                              rtnStruct, resultproc, userDataPtr);
}

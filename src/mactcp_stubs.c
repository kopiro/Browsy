/*
 * dnr.c - MacTCP Domain Name Resolver glue code
 * Based on Apple's DNR library (c) 1989-1995 Apple Computer, Inc.
 * Loads the 'dnrp' code resource from the MacTCP driver file.
 */
#include <Gestalt.h>
#include <Resources.h>
#include <Files.h>
#include <Memory.h>
#include <Traps.h>
#include <ToolUtils.h>
#include "MacTCP.h"
#include "AddressXlation.h"

/* Folders.h redirect -- FindFolder and friends are in Multiverse.h */

static Handle  gDNRCodeHndl = nil;
static ProcPtr gDNRCodePtr  = nil;

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

    if (Gestalt(gestaltFindFolderAttr, &feature) == noErr)
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

    while (PBHGetFInfoSync((HParmBlkPtr)&fi) == noErr) {
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

    /* search Control Panels for MacTCP 1.1 */
    GetCPanelFolder(&vRefNum, &dirID);
    refnum = SearchFolderForDNRP('cdev', 'ztcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    /* search System Folder for MacTCP 1.0.x */
    GetSystemFolder(&vRefNum, &dirID);
    refnum = SearchFolderForDNRP('cdev', 'mtcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    /* search Control Panels for MacTCP 1.0.x */
    GetCPanelFolder(&vRefNum, &dirID);
    refnum = SearchFolderForDNRP('cdev', 'mtcp', vRefNum, dirID);
    if (refnum != -1) return refnum;

    return -1;
}

OSErr OpenResolver(char *fileName)
{
    short refnum;
    OSErr rc;
    (void)fileName;

    if (gDNRCodePtr != nil)
        return noErr;

    refnum = OpenOurRF();

    gDNRCodeHndl = GetIndResource('dnrp', 1);
    if (gDNRCodeHndl == nil)
        return ResError();

    DetachResource(gDNRCodeHndl);
    if (refnum != -1)
        CloseResFile(refnum);

    MoveHHi(gDNRCodeHndl);
    HLock(gDNRCodeHndl);
    gDNRCodePtr = (ProcPtr)*gDNRCodeHndl;

    rc = CallOpenResolverProc(gDNRCodePtr, OPENRESOLVER, fileName);
    if (rc != noErr) {
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
    if (gDNRCodePtr == nil)
        return notOpenErr;

    return CallStrToAddrProc(gDNRCodePtr, STRTOADDR, hostName,
                             rtnStruct, resultproc, userDataPtr);
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

    return CallEnumCacheProc(gDNRCodePtr, ENUMCACHE,
                             resultproc, userDataPtr);
}

OSErr AddrToName(unsigned long addr, struct hostInfo *rtnStruct,
                 ResultUPP resultproc, Ptr userDataPtr)
{
    if (gDNRCodePtr == nil)
        return notOpenErr;

    return CallAddrToNameProc(gDNRCodePtr, ADDRTONAME, addr,
                              rtnStruct, resultproc, userDataPtr);
}

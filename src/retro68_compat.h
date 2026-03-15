/*
 * retro68_compat.h - Compatibility shims for building with Retro68
 * multiversal headers (vs. the older Universal Interfaces).
 */
#ifndef RETRO68_COMPAT_H
#define RETRO68_COMPAT_H

/* Include the multiversal headers if a specific Mac header hasn't
   pulled them in already. */
#ifndef __MULTIVERSE__
#include <Multiverse.h>
#endif

/* ---- Boolean constants ---- */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif

/* ---- EventMask typedef ---- */
/* Multiversal declares the enum values (everyEvent, mUpMask, etc.)
   but never typedefs EventMask itself. */
typedef short EventMask;

/* ---- Control part-code aliases ---- */
/* Old ControlDefinitions.h names -> multiversal enum values */
#define kControlButtonPart      inButton      /* 10 */
#define kControlCheckBoxPart    inCheckBox    /* 11 */
#define kControlIndicatorPart   inThumb       /* 129 */
#define kControlUpButtonPart    inUpButton    /* 20 */
#define kControlDownButtonPart  inDownButton  /* 21 */
#define kControlPageUpPart      inPageUp      /* 22 */
#define kControlPageDownPart    inPageDown    /* 23 */

/* ---- Memory Manager ---- */
#define InlineGetHandleSize  GetHandleSize

/* ---- File Manager sync wrappers ---- */
/* Old API: PBGetCatInfo(pb, async)  -- always called with FALSE */
#define PBGetCatInfo(pb, async)  PBGetCatInfoSync(pb)
#define PBGetWDInfo(pb, async)   PBGetWDInfoSync(pb)
#define PBOpenWD(pb, async)      PBOpenWDSync(pb)

/* ---- Folder Manager constants ---- */
#ifndef kOnSystemDisk
#define kOnSystemDisk  (-32768)
#endif
#ifndef kDontCreateFolder
#define kDontCreateFolder  false
#endif

/* ---- AppleEvents ---- */
/* keyMissedKeywordAttr = 'miss' */
#ifndef keyMissedKeywordAttr
#define keyMissedKeywordAttr  0x6D697373
#endif

/* Old UPP compatibility macro */
#ifndef NewAEEventHandlerProc
#define NewAEEventHandlerProc(proc)  NewAEEventHandlerUPP((AEEventHandlerProcPtr)(proc))
#endif

#endif /* RETRO68_COMPAT_H */

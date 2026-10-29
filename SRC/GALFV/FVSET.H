/* Build 26.10.20.1 12:30PM */
/*****************************************************************************
 *   FVSET.H                                   File Viewer - Live settings    *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   The settings the module runs on live in GALFVSET.DAT.  There are no     *
 *   CNF options: a new install starts from the defaults in FVSET.C, and     *
 *   the sysop changes settings online from the library's sysop menu; every  *
 *   change applies immediately.  Code reads the in-memory copy (fvcfg) at   *
 *   the point of use - never a value cached at startup.                     *
 *                                                                           *
 *   Adding a setting: a field at the END of struct fvsetdat and a row,      *
 *   with its default, in setdefs[] in FVSET.C.  An existing record reads    *
 *   the new field as zero, so bump FVS_LAYOUT and default the field in      *
 *   set_open() if zero is not a good value.                                 *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#ifndef FVSET_H
#define FVSET_H

#include <stddef.h>
#include "FVDEFS.H"

#define FVS_FILE     "GALFVSET.DAT"
#define FVS_RECID    "SETTINGS"      /* key of the one settings record       */
/* 512 bytes, well inside a 1024-byte Btrieve page with its overhead (a    */
/* record as long as the page fails with status 28).                        */
#define FVS_RECLEN   512
#define FVS_LAYOUT   1               /* bump when the record layout changes  */

#define FVS_DIRSIZ   81              /* document folder                      */

#pragma pack(push, 8)
struct fvsetdat {                    /* the settings themselves              */
     SHORT layout;                   /*   FVS_LAYOUT when written            */
     CHAR cfgkey[KEYSIZ];            /*   key for sysop functions            */
     CHAR addkey[KEYSIZ];            /*   key to add documents               */
     CHAR docdir[FVS_DIRSIZ];        /*   where documents are stored         */
     SHORT defmode;                  /*   FVM_PAGED / FVM_FEED / FVM_ASK     */
     SHORT maxkb;                    /*   largest upload, in KB              */
     LONG defbaud;                   /*   simulated baud rate, 0 = off       */
     SHORT rawpath;                  /*   sysops may view any server path    */
};

struct fvsetrec {                    /* the record as stored                 */
     CHAR recid[16];                 /*   FVS_RECID (key 0)                  */
     struct fvsetdat d;
     CHAR spare[FVS_RECLEN - 16 - sizeof(struct fvsetdat)];
};
#pragma pack(pop)
typedef CHAR fvs_chk_size[(sizeof(struct fvsetrec) == FVS_RECLEN) ? 1 : -1];

extern struct fvsetdat fvcfg;        /* live settings - read at point of use */

VOID  set_open(VOID);                /* open file, load or seed settings     */
VOID  set_close(VOID);
VOID  set_save(VOID);                /* write fvcfg to the settings file     */
VOID  set_defaults(VOID);            /* every setting to its default, saved  */
VOID  set_mkdir(VOID);               /* make sure the document folder exists */

/* ---- Online editor support (used by FVSYSOP.C) ----                       */
INT         set_count(VOID);         /* number of settings                   */
const CHAR *set_label(INT idx);      /* idx is 0-based                       */
const CHAR *set_value(INT idx);      /* current value, formatted             */
const CHAR *set_limit(INT idx);      /* what values are allowed              */
INT         set_change(INT idx, const CHAR *text);

#define SETCHG_OK     0              /* set_change() results                 */
#define SETCHG_BAD    1              /*   value not allowed                  */
#define SETCHG_SAME   2              /*   empty input: kept as is            */

/* Shared parsers and names, also used by the document editor.              */
const CHAR *fv_modename(SHORT mode); /* FVM_xxx as text                      */
const CHAR *fv_typename(SHORT type); /* FVY_xxx as text                      */
const CHAR *fv_baudname(LONG baud);  /* "2400", "FULL SPEED", "DEFAULT"      */
INT  fv_parsemode(const CHAR *s, GBOOL dflt);  /* -1 if not recognised       */
INT  fv_parsetype(const CHAR *s);              /* -1 if not recognised       */
LONG fv_parsebaud(const CHAR *s, GBOOL dflt);  /* -2 if not recognised       */
GBOOL fv_goodkey(const CHAR *s);     /* a plausible key name ("" allowed)    */

#endif /* FVSET_H */

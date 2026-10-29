/* Build 26.10.20.1 12:30PM */
/*****************************************************************************
 *   FVSET.C                                   File Viewer - Live settings    *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   Loads, seeds and saves the settings record (GALFVSET.DAT), and          *
 *   describes each setting - its label, its allowed values, where it lives  *
 *   in the record and its default - so the sysop editor in FVSYSOP.C can    *
 *   list and change them generically.                                       *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <direct.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

struct fvsetdat fvcfg;               /* the live settings                    */
static DFAFILE *fvsdat;              /* GALFVSET.DAT                         */

/* ------------------------------------------------------------------------ *
 * The settings table.  The defaults live here, not in GALFV.MSG: settings  *
 * are changed only online, so there is one place to change them.  They     *
 * are used on a new install and by "Reset all settings".                   *
 * ------------------------------------------------------------------------ */
enum { T_KEY, T_DIR, T_MODE, T_NUM, T_BAUD, T_BOOL };

struct setdef {
     INT type;                       /* T_xxx                                */
     size_t offset;                  /* where it lives in struct fvsetdat    */
     INT size;                       /* string buffer size (incl. NUL)       */
     LONG min, max;                  /* numeric range (T_NUM)                */
     LONG defnum;                    /* default for numbers                  */
     const CHAR *defstr;             /* default for text                     */
     const CHAR *label;              /* what the sysop sees                  */
};

#define FVS_OFS(f) offsetof(struct fvsetdat, f)
#define NUM(t, f, lo, hi, def, lbl) { t, FVS_OFS(f), 0, lo, hi, def, NULL, lbl }
#define STR(t, f, sz, def, lbl)     { t, FVS_OFS(f), sz, 0, 0, 0, def, lbl }

static const struct setdef setdefs[] = {
     STR(T_KEY,  cfgkey,  KEYSIZ,      "SYSOP", "Key for sysop functions"),
     STR(T_KEY,  addkey,  KEYSIZ,      "SYSOP", "Key to add documents"),
     STR(T_DIR,  docdir,  FVS_DIRSIZ,  "GALFV", "Folder for uploaded documents"),
     NUM(T_MODE, defmode, 0, 0,     FVM_PAGED,  "Default display mode"),
     NUM(T_NUM,  maxkb,   1, 32767, 1024,       "Largest upload, in KB"),
     NUM(T_BAUD, defbaud, 0, 0,     0,          "Default simulated baud rate"),
     NUM(T_BOOL, rawpath, 0, 0,     1,          "Sysops may view any server file"),
};
#define NSET ((INT)(sizeof(setdefs) / sizeof(setdefs[0])))

#define WHERE(d) ((CHAR *)&fvcfg + (d)->offset)

/* ------------------------------------------------------------------------ *
 * Names and parsers shared with the document editor.                       *
 * ------------------------------------------------------------------------ */

const CHAR *
fv_modename(SHORT mode)
{
     switch (mode) {
     case FVM_PAGED: return "PAGED";
     case FVM_FEED:  return "CONTINUOUS";
     case FVM_ASK:   return "ASK";
     default:        return "DEFAULT";
     }
}

const CHAR *
fv_typename(SHORT type)
{
     switch (type) {
     case FVY_TEXT: return "TEXT";
     case FVY_ANSI: return "ANSI";
     case FVY_IBM:  return "IBM";
     default:       return "AUTO";
     }
}

const CHAR *
fv_baudname(LONG baud)               /* a document's or the module's rate    */
{
     static CHAR buf[24];

     if (baud == 0) {
          return "DEFAULT";
     }
     if (baud < 0) {
          return "FULL SPEED";
     }
     sprintf(buf, "%ld", baud);
     return buf;
}

INT
fv_parsemode(const CHAR *s, GBOOL dflt)   /* dflt: DEFAULT is allowed        */
{
     if (sameto(s, "PAGED") || sameas(s, "P")) {
          return FVM_PAGED;
     }
     if (sameto(s, "CONTINUOUS") || sameas(s, "C") || sameas(s, "FEED")) {
          return FVM_FEED;
     }
     if (sameto(s, "ASK") || sameas(s, "A")) {
          return FVM_ASK;
     }
     if (dflt && (sameto(s, "DEFAULT") || sameas(s, "D"))) {
          return FVM_DEFAULT;
     }
     return -1;
}

INT
fv_parsetype(const CHAR *s)
{
     if (sameas(s, "AUTO"))  return FVY_AUTO;
     if (sameas(s, "TEXT") || sameas(s, "TXT")) return FVY_TEXT;
     if (sameas(s, "ANSI"))  return FVY_ANSI;
     if (sameas(s, "IBM"))   return FVY_IBM;
     return -1;
}

LONG
fv_parsebaud(                        /* a baud rate typed by a user          */
const CHAR *s,
GBOOL dflt)                          /*   TRUE: a document's (DEFAULT = 0,   */
                                     /*   FULL = -1); FALSE: the module's    */
                                     /*   (OFF/FULL/0 = 0)                   */
{
     LONG n;
     const CHAR *p;

     if (sameto(s, "FULL") || sameas(s, "OFF") || sameas(s, "NONE")) {
          return dflt ? -1L : 0L;
     }
     if (dflt && (sameto(s, "DEFAULT") || sameas(s, "D"))) {
          return 0L;
     }
     for (p = s ; *p != '\0' ; p++) {
          if (!isdigit((UCHAR)*p)) {
               return -2L;
          }
     }
     if (*s == '\0') {
          return -2L;
     }
     n = atol(s);
     if (n == 0) {
          return dflt ? -1L : 0L;    /* 0 typed: no simulation               */
     }
     if (n < FV_MINBAUD || n > FV_MAXBAUD) {
          return -2L;
     }
     return n;
}

GBOOL
fv_goodkey(const CHAR *s)            /* letters, digits and a few symbols    */
{
     if (strlen(s) >= KEYSIZ) {
          return FALSE;
     }
     for ( ; *s != '\0' ; s++) {
          if (!isgraph((UCHAR)*s)) {
               return FALSE;
          }
     }
     return TRUE;
}

static GBOOL
gooddir(const CHAR *s)               /* a folder name we are willing to use  */
{
     if (*s == '\0' || strstr(s, "..") != NULL) {
          return FALSE;
     }
     for ( ; *s != '\0' ; s++) {
          if (!isprint((UCHAR)*s) || strchr("*?\"<>|", *s) != NULL) {
               return FALSE;
          }
     }
     return TRUE;
}

/* ------------------------------------------------------------------------ *
 * File handling.                                                           *
 * ------------------------------------------------------------------------ */

VOID
set_save(VOID)                       /* write fvcfg to GALFVSET.DAT          */
{
     struct fvsetrec rec;

     /* rec.recid is the lookup key: it must be writable memory (see      */
     /* set_open).                                                         */
     setmem(&rec, sizeof(rec), 0);
     stzcpy(rec.recid, FVS_RECID, sizeof(rec.recid));
     fvcfg.layout = FVS_LAYOUT;
     rec.d = fvcfg;
     dfaSetBlk(fvsdat);
     if (dfaAcqEQ(NULL, rec.recid, 0)) {
          dfaUpdate(&rec);
     }
     else {
          dfaInsert(&rec);
     }
     dfaRstBlk();
}

static VOID
set_sanity(VOID)                     /* keep loaded values inside limits     */
{
     INT i;

     for (i = 0 ; i < NSET ; i++) {
          const struct setdef *d = &setdefs[i];
          CHAR *w = WHERE(d);

          switch (d->type) {
          case T_NUM:
               if (*(SHORT *)w < d->min || *(SHORT *)w > d->max) {
                    *(SHORT *)w = (SHORT)d->defnum;
               }
               break;
          case T_MODE:
               if (*(SHORT *)w < FVM_PAGED || *(SHORT *)w > FVM_ASK) {
                    *(SHORT *)w = (SHORT)d->defnum;
               }
               break;
          case T_BAUD:
               if (*(LONG *)w != 0
                && (*(LONG *)w < FV_MINBAUD || *(LONG *)w > FV_MAXBAUD)) {
                    *(LONG *)w = 0;
               }
               break;
          case T_BOOL:
               *(SHORT *)w = (*(SHORT *)w != 0);
               break;
          case T_DIR:
               w[d->size - 1] = '\0';
               if (!gooddir(w)) {
                    stzcpy(w, d->defstr, d->size);
               }
               break;
          default:                   /* strings: guarantee termination       */
               w[d->size - 1] = '\0';
               if (*w == '\0' && d->offset == FVS_OFS(cfgkey)) {
                    stzcpy(w, d->defstr, d->size);
               }
               break;
          }
     }
}

VOID
set_defaults(VOID)                   /* every setting to its default, saved  */
{
     INT i;

     setmem(&fvcfg, sizeof(fvcfg), 0);
     for (i = 0 ; i < NSET ; i++) {
          const struct setdef *d = &setdefs[i];
          CHAR *w = WHERE(d);

          switch (d->type) {
          case T_NUM:
          case T_MODE:
          case T_BOOL:
               *(SHORT *)w = (SHORT)d->defnum;
               break;
          case T_BAUD:
               *(LONG *)w = d->defnum;
               break;
          default:
               stzcpy(w, d->defstr, d->size);
               break;
          }
     }
     set_sanity();
     set_save();
     set_mkdir();
}

VOID
set_mkdir(VOID)                      /* create the document folder if needed */
{
     CHAR dir[FVS_DIRSIZ];
     size_t n;

     stzcpy(dir, fvcfg.docdir, sizeof(dir));
     n = strlen(dir);
     if (n > 0 && (dir[n - 1] == '\\' || dir[n - 1] == '/')) {
          dir[n - 1] = '\0';
     }
     if (dir[0] != '\0' && !isfile(dir)) {
          _mkdir(dir);
     }
}

VOID
set_open(VOID)                       /* open file; load settings or seed them*/
{
     struct fvsetrec rec;

     if (!isfile(FVS_FILE)) {
          struct dfaSegSpec seg;
          struct dfaKeySpec key;

          setmem(&seg, sizeof(seg), 0);
          seg.position = offsetof(struct fvsetrec, recid);
          seg.length = sizeof(rec.recid);
          seg.type = DFAST_ZSTRING;
          setmem(&key, sizeof(key), 0);
          key.flags = 0;
          key.nSegments = 1;
          key.segs = &seg;
          dfaCreateSpec(FVS_FILE, FALSE, sizeof(struct fvsetrec), 1024,
                        0, 0, 1, &key, NULL);
     }
     fvsdat = dfaOpen(FVS_FILE, sizeof(struct fvsetrec), NULL);

     /* The key must be in writable memory.  dfaAcqEQ copies only as many   */
     /* key bytes as goodblk() allows, and goodblk() returns 0 for          */
     /* read-only memory (WG33 ISGOODPT.C, DFAAPI.C dfaAcqLock), so a       */
     /* string constant would search for an empty key and miss.             */
     setmem(&rec, sizeof(rec), 0);
     stzcpy(rec.recid, FVS_RECID, sizeof(rec.recid));
     dfaSetBlk(fvsdat);
     if (dfaAcqEQ(&rec, rec.recid, 0)) {
          dfaRstBlk();
          fvcfg = rec.d;
          set_sanity();
          set_mkdir();
          return;
     }
     dfaRstBlk();
     set_defaults();                 /* a new install: the defaults          */
     shocst("GALFV SETTINGS CREATED", "New settings file, default values");
}

VOID
set_close(VOID)
{
     dfaClose(fvsdat);
}

/* ------------------------------------------------------------------------ *
 * Editor support.                                                          *
 * ------------------------------------------------------------------------ */

INT
set_count(VOID)
{
     return NSET;
}

const CHAR *
set_label(INT idx)
{
     return setdefs[idx].label;
}

const CHAR *
set_value(INT idx)                   /* current value, ready for display     */
{
     static CHAR buf[FVS_DIRSIZ + 8];
     const struct setdef *d = &setdefs[idx];
     CHAR *w = WHERE(d);

     switch (d->type) {
     case T_NUM:
          sprintf(buf, "%d", *(SHORT *)w);
          break;
     case T_MODE:
          strcpy(buf, fv_modename(*(SHORT *)w));
          break;
     case T_BAUD:
          strcpy(buf, *(LONG *)w == 0 ? "OFF (full speed)"
                                      : fv_baudname(*(LONG *)w));
          break;
     case T_BOOL:
          strcpy(buf, *(SHORT *)w ? "YES" : "NO");
          break;
     default:
          if (*w == '\0') {
               strcpy(buf, "(none)");
          }
          else {
               stzcpy(buf, w, sizeof(buf));
          }
          break;
     }
     return buf;
}

const CHAR *
set_limit(INT idx)                   /* describe the allowed values          */
{
     static CHAR buf[96];
     const struct setdef *d = &setdefs[idx];

     switch (d->type) {
     case T_NUM:
          sprintf(buf, "a number from %ld to %ld", d->min, d->max);
          break;
     case T_MODE:
          strcpy(buf, "PAGED, CONTINUOUS or ASK");
          break;
     case T_BAUD:
          sprintf(buf, "OFF, or a rate from %ld to %ld (300, 1200, 2400...)",
                  FV_MINBAUD, FV_MAXBAUD);
          break;
     case T_BOOL:
          strcpy(buf, "YES or NO");
          break;
     case T_KEY:
          sprintf(buf, "a key name, up to %d characters, no spaces",
                  d->size - 1);
          break;
     default:
          sprintf(buf, "a folder, relative to the BBS folder or a full path, "
                       "up to %d characters", d->size - 1);
          break;
     }
     return buf;
}

INT
set_change(                          /* apply a new value typed by the sysop */
INT idx,
const CHAR *text)
{
     const struct setdef *d = &setdefs[idx];
     CHAR *w = WHERE(d);
     LONG n;
     INT m;

     if (*text == '\0') {
          return SETCHG_SAME;
     }
     switch (d->type) {
     case T_NUM:
          n = atol(text);
          if (!isdigit((UCHAR)*text) || n < d->min || n > d->max) {
               return SETCHG_BAD;
          }
          *(SHORT *)w = (SHORT)n;
          break;
     case T_MODE:
          if ((m = fv_parsemode(text, FALSE)) < 0) {
               return SETCHG_BAD;
          }
          *(SHORT *)w = (SHORT)m;
          break;
     case T_BAUD:
          if ((n = fv_parsebaud(text, FALSE)) < 0) {
               return SETCHG_BAD;
          }
          *(LONG *)w = n;
          break;
     case T_BOOL:
          if (toupper((UCHAR)*text) == 'Y') {
               *(SHORT *)w = 1;
          }
          else if (toupper((UCHAR)*text) == 'N') {
               *(SHORT *)w = 0;
          }
          else {
               return SETCHG_BAD;
          }
          break;
     case T_KEY:
          if (sameas(text, "-")) {   /* a single - clears it                 */
               text = "";
          }
          if (!fv_goodkey(text)) {
               return SETCHG_BAD;
          }
          /* An empty sysop key would make every caller a sysop.            */
          if (*text == '\0' && d->offset == FVS_OFS(cfgkey)) {
               return SETCHG_BAD;
          }
          stzcpy(w, text, d->size);
          break;
     default:                        /* T_DIR                             */
          if (!gooddir(text) || strlen(text) >= (size_t)d->size) {
               return SETCHG_BAD;
          }
          stzcpy(w, text, d->size);
          set_mkdir();
          break;
     }
     set_save();
     return SETCHG_OK;
}

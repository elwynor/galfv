/* Build 26.10.27.1 08:06PM */
/*****************************************************************************
 *   FVDEFS.H                                   File Viewer - Shared defines  *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *   Originally (C) Copyright 1995 High Velocity Software, Inc.              *
 *                                                                           *
 *   Declarations shared by every File Viewer source file: the version, the  *
 *   user-state numbers, the per-user work area, and the prototypes of each  *
 *   source file's public routines.                                          *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0 - see the     *
 *   LICENSE file in the project root.                                       *
 *****************************************************************************/

#ifndef FVDEFS_H
#define FVDEFS_H

#include "gcomm.h"
#include "majorbbs.h"
#include "GALFV.H"                   /* message numbers for GALFV.MSG       */
#include "FVDOC.H"                   /* the document library record          */

/* ------------------------------------------------------------------------ *
 * Version.  GALFV_VERSION is what sysops see (audit trail, menus); the      *
 * numeric parts feed the DLL's Windows version resource (GALFV.rc).         *
 * ------------------------------------------------------------------------ */
#define GALFV_VER_MAJOR 1
#define GALFV_VER_MINOR 3
#define GALFV_VER_PATCH 4
#define GALFV_VERSION   "1.3.4"

/* ------------------------------------------------------------------------ *
 * Viewer limits.                                                           *
 * ------------------------------------------------------------------------ */
#define FV_MAXPAGE  2000             /* pages we can remember (for Previous) */
#define FV_PGBUF    4096             /* one page (or feed chunk) of output   */
#define FV_NKW      6                /* keywords in one search               */
#define FV_KWSIZ    41               /* longest keyword + 1                  */
#define FV_EDTSIZ   8192             /* largest document written online      */
#define FV_LISTPG   15               /* documents per page of the list       */
#define FV_FEEDLN   20               /* lines per chunk in continuous mode   */
#define FV_SRCHPC   25               /* pages searched per polling cycle     */
#define FV_HMINDOC  5                /* fewest document lines under a header */
                                     /*   kept on screen; less: shown once   */
#define FV_HMAXCHK  16               /* most output chunks a header is       */
                                     /*   measured over (taller: shown once) */

/* ------------------------------------------------------------------------ *
 * User states (usrptr->substt).  0 is "just arrived from the menu".        *
 * ------------------------------------------------------------------------ */
#define ST_ENTRY        0            /* first call: decide what to do        */
#define ST_LIBRARY      1            /* at the document library prompt       */
#define ST_ASKMODE      2            /* "page by page or continuous?"        */
#define ST_PAGER        3            /* at the page prompt                   */
#define ST_KWDS         4            /* typing search keywords               */
#define ST_KWDIR        5            /* "search forward or backward?"        */
#define ST_BUSY         6            /* a page is being sent or searched for */
#define ST_FEED         7            /* continuous feed running              */
#define ST_DONE         8            /* "end of document, press Enter"       */
#define ST_RAWPATH      9            /* sysop: path of a server file to view */
#define ST_HDRLIST      10           /* at the header section prompt         */
#define ST_CREATEQ      11           /* menu link names a missing document   */
                                     /*   (a library state: answered in      */
                                     /*   FVLIB.C, so below ST_ADDNAME)      */
#define ST_ADDNAME      20           /* new document: its name               */
#define ST_ADDTITLE     21           /* new document: its title              */
#define ST_ADDSRC       22           /* new document: upload, write or link? */
#define ST_ADDPATH      23           /* new document: path of a linked file  */
#define ST_UPLOAD       24           /* in the engine's file transfer        */
#define ST_DOCSEL       25           /* which document to edit               */
#define ST_DOCMENU      26           /* a document's property menu           */
#define ST_DOCVAL       27           /* new value for one property           */
#define ST_DOCDEL       28           /* which document to delete             */
#define ST_DELCONF      29           /* confirm deleting a document          */
#define ST_REPLSRC      31           /* replace content: upload or write?    */
#define ST_SETPAGE      40           /* sysop settings page                  */
#define ST_SETVAL       41           /* new value for one setting            */
#define ST_SETRST       42           /* confirm resetting every setting      */

/* Where the viewer goes when the user leaves a document.                   */
#define FVBACK_EXIT     0            /* back to the BBS menu (menu link)     */
#define FVBACK_LIBRARY  1            /* back to the document library         */
#define FVBACK_DOCMENU  2            /* back to the document's property menu */

/* What happens when the output queue has finished sending.                 */
#define FVAFT_PROMPT    0            /* show the page prompt                 */
#define FVAFT_FEED      1            /* send the next chunk of the feed      */
#define FVAFT_HDR       2            /* send the next chunk of the header    */

/* How a document's header is shown (see FVVIEW.C).                         */
#define FVH_NONE        0            /* no header                            */
#define FVH_PIN         1            /* ANSI: drawn once, kept at the top    */
                                     /*   while the document scrolls below   */
#define FVH_EACH        2            /* no ANSI: at the top of every page    */
#define FVH_ONCE        3            /* too tall to keep: shown once, first  */

/* Content types, after AUTO has been resolved (see FVVIEW.C).              */
#define FVT_TEXT        1            /* plain text: word-wrapped             */
#define FVT_ANSI        2            /* ANSI: escape sequences kept          */
#define FVT_IBM         3            /* IBM PC (CP437) art: not wrapped      */

/* ------------------------------------------------------------------------ *
 * The page builder's state for one pass over the file (see FVVIEW.C).      *
 * ------------------------------------------------------------------------ */
struct fvview {
     CHAR path[FV_PTHSIZ];           /* file being shown                     */
     CHAR title[FV_TITSIZ];          /* title shown in prompts               */
     SHORT type;                     /* FVT_xxx, resolved                    */
     SHORT ansi;                     /* user's terminal does ANSI?           */
     SHORT wrapw;                    /* word-wrap width (text)               */
     SHORT termw;                    /* terminal width (ANSI/IBM)            */
     SHORT lines;                    /* text lines per page                  */
     SHORT after;                    /* FVAFT_xxx when output is sent        */
     LONG fsize;                     /* file size, for the percentage        */
     INT cpage;                      /* page on screen (0-based)             */
     INT known;                      /* ppos[0..known] are known             */
     INT lastpg;                     /* last page index, or -1 unknown       */
     LONG ppos[FV_MAXPAGE];          /* file offset where each page starts   */
     LONG pgend;                     /* file offset just after this page     */
     LONG feedpos;                   /* continuous mode: next offset to read */
     SHORT feedeof;                  /*   the last chunk has been queued     */
     SHORT searching;                /* a keyword search is running          */
     SHORT rush;                     /* send the rest of this page at once   */
     SHORT opts;                     /* FVO_xxx from vw_open()               */
     LONG baud;                      /* simulated baud rate, 0 = full speed  */
     ULONG lastms;                   /*   clock (ms) when credit was topped  */
     LONG credit;                    /*   bytes we may send now, x 1000      */
     INT outlen;                     /* bytes waiting in out[]               */
     INT outpos;                     /*   of which this many are sent        */
     CHAR out[FV_PGBUF];             /* output queue                         */
     CHAR kwds[FV_NKW][FV_KWSIZ];    /* search keywords                      */
     SHORT nkw;                      /*   how many                           */
     SHORT kwdir;                    /*   'F' or 'B'                         */
     SHORT kwact;                    /*   a search is active (Enter=again)   */
     INT kwpage;                     /*   next page to search                */
     CHAR found[FV_KWSIZ];           /*   the keyword that was found         */
     SHORT rows;                     /* screen rows                          */
     CHAR hpath[FV_FULSIZ];          /* header file, "" = none               */
     SHORT htype;                    /*   FVT_xxx, resolved                  */
     SHORT hmode;                    /*   FVH_xxx                            */
     SHORT hrows;                    /*   height in screen lines             */
     SHORT hpart;                    /*   last line has no line ending       */
     SHORT hshown;                   /*   drawn since the document opened    */
     SHORT hend;                     /*   its last chunk has been queued     */
     SHORT hthen;                    /*   FVAFT_xxx: what follows it         */
     SHORT hfresh;                   /*   the next page follows it directly  */
     LONG hpos;                      /*   next file offset to draw           */
};

struct fvhead {                      /* a header for vw_open()               */
     CHAR path[FV_FULSIZ];           /*   its file                           */
     SHORT type;                     /*   FVY_xxx                            */
     SHORT rows;                     /*   height set by hand, 0 = count it   */
};

/* ------------------------------------------------------------------------ *
 * Per-user work area.  The engine gives each channel a "volatile data"      *
 * area (vdaptr) for whatever module the user is in; we size it with         *
 * dclvda() at startup.  The engine clears it each time a user enters.       *
 * The viewer and the online editor are never in use at the same time, so   *
 * they share space.                                                         *
 * ------------------------------------------------------------------------ */
struct fvvda {
     SHORT home;                     /* FVBACK_EXIT or FVBACK_LIBRARY: where */
                                     /*   the user goes when finished        */
     SHORT back;                     /* FVBACK_xxx: where the viewer returns */
     SHORT mode;                     /* chosen FVM_PAGED / FVM_FEED          */
     INT listofs;                    /* first document shown in the list     */
     INT item;                       /* property or setting being changed    */
     SHORT inhdrs;                   /* working in the header section        */
     CHAR docnam[FV_NAMSIZ];         /* document being viewed or edited      */
     struct fvdoc doc;               /* working copy while adding or editing */
     SHORT isnew;                    /* doc is being added, not edited       */
     SHORT upok;                     /* an upload arrived safely             */
     CHAR uptmp[FV_FULSIZ];          /* where an upload is being received    */
     CHAR upnew[FV_NAMSIZ + 8];      /* the file name it will be given       */
     union {
          struct fvview v;           /* the viewer                           */
          CHAR edt[FV_EDTSIZ];       /* the online editor's text             */
     } u;
};
#define fvv ((struct fvvda *)vdaptr)

/* ---------------------------------------------------------------- GALFV.C */
extern HMCVFILE fvmb;                /* our message file                     */
extern INT fvstt;                    /* our module (state) number            */
extern FILE *fvfp[MAXNTERM];         /* the file each channel is viewing     */
VOID fv_guard(GBOOL on);             /* block pages/global cmds while viewing*/

/* ---------------------------------------------------------------- FVLIB.C */
GBOOL lib_entry(VOID);               /* first call, from the menu            */
GBOOL lib_input(VOID);               /* input at the library and its dialogs */
VOID  lib_prompt(VOID);              /* show the library prompt              */
GBOOL lib_back(VOID);                /* after a document: return to caller   */
GBOOL lib_home(VOID);                /* finished: library, or out to the menu*/
GBOOL lib_opendoc(struct fvdoc *d, SHORT back);  /* view a library document */
GBOOL lib_canview(const struct fvdoc *d);  /* may this user view it?         */
GBOOL lib_issysop(VOID);             /* holds the sysop key?                 */
GBOOL lib_canadd(VOID);              /* holds the add-documents key?         */
VOID  lib_headers(VOID);             /* list the headers, then their prompt  */

/* --------------------------------------------------------------- FVVIEW.C */
VOID  fv_out(VOID);                  /* outprf() then empty prfbuf: use this */
GBOOL vw_open(const CHAR *path, const CHAR *title, SHORT type, SHORT mode,
              LONG baud, SHORT back, SHORT opts,
              const struct fvhead *hd);  /* start viewing a file (hd: NULL)   */

#define FVO_QUIET   0x01             /* vw_open opts: no feed header/"Stopped"*/
#define FVO_RETURN  0x02             /*   finished: straight back, no prompt */
GBOOL vw_input(VOID);                /* input while viewing                  */
VOID  vw_close(VOID);                /* stop viewing, close the file         */
VOID  vw_hangup(INT unum);           /* channel hung up: tidy up             */

/* --------------------------------------------------------------- FVPAGE.C */
INT   pg_build(LONG start, CHAR *buf, INT maxlines, GBOOL ffbreak,
               LONG *endpos, GBOOL *eof);  /* screen output from the file    */
INT   pg_match(const CHAR *s);       /* keyword found in a page: index or -1 */
SHORT vw_detect(const CHAR *path);   /* guess FVT_xxx from name and content  */
extern INT pg_lines;                 /* last pg_build(): lines completed     */
extern GBOOL pg_part;                /*   and a last line without an ending  */

/* --------------------------------------------------------------- FVEDIT.C */
VOID  ed_add(const CHAR *name, SHORT kind);  /* start adding: FVK_xxx      */
GBOOL ed_pick(const CHAR *name, GBOOL del);  /* change (or delete) by name */
GBOOL ed_input(VOID);                /* input in the add/edit dialogs        */
VOID  ed_menu(VOID);                 /* show the document property menu      */

/* -------------------------------------------------------------- FVSYSOP.C */
VOID  sy_settings(VOID);             /* show the settings page               */
GBOOL sy_input(VOID);                /* input in the settings editor         */

/* -------------------------------------------------------------- FVTVARS.C */
VOID tv_init(VOID);                  /* register our text variables          */
VOID tv_set(INT which, const CHAR *value);
VOID tv_setn(INT which, LONG value);

enum {                               /* text variables (GALFV_xxx)           */
     TV_DOC, TV_TITLE, TV_PAGE, TV_PCT, TV_NUM, TV_ITEM, TV_LABEL, TV_VALUE,
     TV_LIMIT, TV_FILE, TV_WORD, TV_COUNT, TV_OWNER, TV_BAUD,
     TV_COUNT_OF
};

#endif /* FVDEFS_H */

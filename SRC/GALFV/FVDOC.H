/* Build 26.10.21.2 10:40AM */
/*****************************************************************************
 *   FVDOC.H                                  File Viewer - Document library  *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   A "document" is a named entry in GALFVDOC.DAT that points at a file.    *
 *   Menu links name a document in their command string, users pick one     *
 *   from the library list, and authors add documents by uploading a file,   *
 *   writing one online, or (sysops) linking a file already on the server.   *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#ifndef FVDOC_H
#define FVDOC_H

#include "gcomm.h"
#include "majorbbs.h"

#define FVD_FILE     "GALFVDOC.DAT"
#define FVD_RECLEN   512             /* fixed record length, room to grow    */
#define FVD_LAYOUT   3               /* bump when the record layout changes  */
                                     /*   2 (1.2.2): added quiet, autoret    */
                                     /*   3 (1.3.0): added kind, header, rows*/

#define FV_NAMSIZ    16              /* document name, upper case, + NUL     */
#define FV_TITSIZ    61              /* title shown in the list              */
#define FV_PTHSIZ    128             /* file name or path                    */
#define FV_FULSIZ    224             /* document folder + file name          */

/* Display mode: how the document is shown.                                 */
#define FVM_DEFAULT  0               /* use the module setting               */
#define FVM_PAGED    1               /* page by page, with a prompt          */
#define FVM_FEED     2               /* continuous, no pauses                */
#define FVM_ASK      3               /* ask the user each time               */

/* Content type: how the bytes are treated.                                 */
#define FVY_AUTO     0               /* decide from the name and content     */
#define FVY_TEXT     1
#define FVY_ANSI     2
#define FVY_IBM      3

/* Record kind.  A header is shown at the top of the documents that name   */
/* it; headers live in the same file (and share the names) as documents,    */
/* and are added, changed and deleted the same way.                          */
#define FVK_DOC      0               /* a document (the 1.2 records)         */
#define FVK_HEADER   1               /* a header shared by documents         */
#define FV_MAXHROWS  99              /* tallest header height set by hand    */

/* Simulated baud rate limits.  0 in the module setting means full speed.   */
#define FV_MINBAUD   110L
#define FV_MAXBAUD   115200L

/* Pin the packing so the stored record never depends on compiler options. */
#pragma pack(push, 8)
struct fvdoc {
     CHAR name[FV_NAMSIZ];           /* key 0: name, upper case              */
     SHORT layout;                   /* FVD_LAYOUT when written              */
     CHAR title[FV_TITSIZ];          /* shown in the list and prompts        */
     CHAR file[FV_PTHSIZ];           /* plain name = in the document folder; */
                                     /*   otherwise a path (linked file)     */
     CHAR key[KEYSIZ];               /* key needed to view ("" = anyone)     */
     SHORT mode;                     /* FVM_xxx                              */
     SHORT type;                     /* FVY_xxx                              */
     SHORT hidden;                   /* 1 = not listed (menu links only)     */
     SHORT linked;                   /* 1 = file is a sysop-linked path      */
     CHAR owner[UIDSIZ];             /* who added it                         */
     USHORT created;                 /* date added (packed, today())         */
     USHORT changed;                 /* date last changed                    */
     LONG views;                     /* times opened                         */
     LONG baud;                      /* simulated baud rate: 0 = the module  */
                                     /*   default, -1 = full speed           */
     /* Layout 2 (1.2.2).  Zero means the 1.2.0 behaviour, so older records */
     /* read correctly as they are.                                         */
     SHORT quiet;                    /* 1 = no continuous-display header or  */
                                     /*   "Stopped" line: only the file      */
     SHORT autoret;                  /* 1 = when finished, go straight back  */
                                     /*   (no "End of ... Press Enter")      */
     /* Layout 3 (1.3.0).  Zero/empty means a document with no header.     */
     SHORT kind;                     /* FVK_xxx                              */
     CHAR header[FV_NAMSIZ];         /* document: its header's name, or ""   */
     SHORT rows;                     /* header: height in lines, 0 = count   */
     CHAR spare[212];                /* room to grow: the fields above end   */
                                     /*   at offset 300 (KEYSIZ 16, UIDSIZ   */
                                     /*   30, one alignment byte after key)  */
};
#pragma pack(pop)

/* The record must stay exactly FVD_RECLEN bytes.  A new field comes out of */
/* spare[]; if this stops compiling, fix the spare size - never the length. */
typedef CHAR fvd_chk_size[(sizeof(struct fvdoc) == FVD_RECLEN) ? 1 : -1];

VOID  doc_open(VOID);                /* open (create) GALFVDOC.DAT           */
VOID  doc_close(VOID);
GBOOL doc_get(const CHAR *name, struct fvdoc *d);    /* by name             */
GBOOL doc_next(const CHAR *after, struct fvdoc *d);  /* next name, "" = 1st */
GBOOL doc_insert(struct fvdoc *d);   /* FALSE if the name is taken           */
GBOOL doc_update(struct fvdoc *d);
GBOOL doc_delete(const CHAR *name);
VOID  doc_counted(const CHAR *name); /* add one to its view count            */
GBOOL doc_goodname(const CHAR *name);/* a valid document name?               */
VOID  doc_path(const struct fvdoc *d, CHAR *buf, INT size);  /* file to open */

#endif /* FVDOC_H */

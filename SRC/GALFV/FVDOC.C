/* Build 26.10.20.1 12:30PM */
/*****************************************************************************
 *   FVDOC.C                                  File Viewer - Document library  *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   Reads and writes GALFVDOC.DAT, the list of named documents.  Names are  *
 *   stored in upper case, so lookups are case-insensitive for users.        *
 *                                                                           *
 *   Every Btrieve lookup key is copied into a local buffer first: the       *
 *   engine only reads keys from writable memory, so a string constant or a  *
 *   const parameter could silently search for an empty key.                 *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

static DFAFILE *fvddat;              /* GALFVDOC.DAT                         */

static VOID
mkkey(CHAR *key, const CHAR *name)   /* a writable, upper-case lookup key    */
{
     setmem(key, FV_NAMSIZ, 0);
     stzcpy(key, name, FV_NAMSIZ);
     strupr(key);
}

VOID
doc_open(VOID)                       /* open GALFVDOC.DAT, creating it       */
{
     if (!isfile(FVD_FILE)) {
          struct dfaSegSpec seg;
          struct dfaKeySpec key;

          setmem(&seg, sizeof(seg), 0);
          seg.position = offsetof(struct fvdoc, name);
          seg.length = FV_NAMSIZ;
          seg.type = DFAST_ZSTRING;
          setmem(&key, sizeof(key), 0);
          key.flags = 0;
          key.nSegments = 1;
          key.segs = &seg;
          dfaCreateSpec(FVD_FILE, FALSE, sizeof(struct fvdoc), 1024,
                        0, 0, 1, &key, NULL);
     }
     fvddat = dfaOpen(FVD_FILE, sizeof(struct fvdoc), NULL);
}

VOID
doc_close(VOID)
{
     dfaClose(fvddat);
}

GBOOL
doc_get(const CHAR *name, struct fvdoc *d)     /* look a document up by name */
{
     CHAR key[FV_NAMSIZ];
     GBOOL ok;

     if (!doc_goodname(name)) {
          return FALSE;
     }
     mkkey(key, name);
     dfaSetBlk(fvddat);
     ok = dfaAcqEQ(d, key, 0);
     dfaRstBlk();
     return ok;
}

GBOOL
doc_next(                            /* the document after a name, in order  */
const CHAR *after,                   /*   "" for the first document          */
struct fvdoc *d)
{
     CHAR key[FV_NAMSIZ];
     GBOOL ok;

     dfaSetBlk(fvddat);
     if (*after == '\0') {
          ok = dfaAcqLO(d, 0);
     }
     else {
          mkkey(key, after);
          ok = dfaAcqGT(d, key, 0);
     }
     dfaRstBlk();
     return ok;
}

GBOOL
doc_insert(struct fvdoc *d)          /* add a new document                   */
{
     struct fvdoc old;

     strupr(d->name);
     if (doc_get(d->name, &old)) {
          return FALSE;
     }
     d->layout = FVD_LAYOUT;
     dfaSetBlk(fvddat);
     dfaInsert(d);
     dfaRstBlk();
     return TRUE;
}

GBOOL
doc_update(struct fvdoc *d)          /* save changes to an existing document */
{
     CHAR key[FV_NAMSIZ];
     struct fvdoc old;
     GBOOL ok;

     mkkey(key, d->name);
     d->layout = FVD_LAYOUT;
     dfaSetBlk(fvddat);
     if ((ok = dfaAcqEQ(&old, key, 0)) != FALSE) {
          dfaUpdate(d);
     }
     dfaRstBlk();
     return ok;
}

GBOOL
doc_delete(const CHAR *name)
{
     CHAR key[FV_NAMSIZ];
     struct fvdoc old;
     GBOOL ok;

     mkkey(key, name);
     dfaSetBlk(fvddat);
     if ((ok = dfaAcqEQ(&old, key, 0)) != FALSE) {
          dfaDelete();
     }
     dfaRstBlk();
     return ok;
}

VOID
doc_counted(const CHAR *name)        /* one more view                        */
{
     struct fvdoc d;

     if (doc_get(name, &d)) {
          d.views++;
          doc_update(&d);
     }
}

GBOOL
doc_goodname(const CHAR *name)       /* 2-15 letters, digits, - or _         */
{
     size_t n = strlen(name);

     /* At least two characters, so a name never looks like a one-letter   */
     /* library command.                                                     */
     if (n < 2 || n >= FV_NAMSIZ || !isalnum((UCHAR)*name)) {
          return FALSE;
     }
     for ( ; *name != '\0' ; name++) {
          if (!isalnum((UCHAR)*name) && *name != '-' && *name != '_') {
               return FALSE;
          }
     }
     return TRUE;
}

VOID
doc_path(                            /* the file to open for a document      */
const struct fvdoc *d,
CHAR *buf,
INT size)
{
     size_t n;

     if (d->linked) {                /* a sysop-linked path: use as given    */
          stzcpy(buf, d->file, size);
          return;
     }
     stzcpy(buf, fvcfg.docdir, size);
     n = strlen(buf);
     if (n > 0 && buf[n - 1] != '\\' && buf[n - 1] != '/') {
          stzcpy(buf + n, "\\", size - (INT)n);
          n++;
     }
     stzcpy(buf + n, d->file, size - (INT)n);
}

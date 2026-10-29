/* Build 26.10.21.4 07:55PM */
/*****************************************************************************
 *   FVEDIT.C                               File Viewer - Managing documents  *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   Adding, changing and deleting library documents.  A document's content *
 *   can come from three places:                                             *
 *     Upload  the BBS's own file transfer (any protocol the board offers)   *
 *             receives a file into the document folder.                     *
 *     Write   the BBS editor (full screen, or the line editor for callers   *
 *             without ANSI) writes a text file into the document folder.    *
 *     Link    (sysops only) the document points at a file already on the    *
 *             server, wherever it is.                                       *
 *                                                                           *
 *   Users holding the add-documents key may add documents and manage the    *
 *   ones they added.  Holders of the sysop key may manage every document.   *
 *                                                                           *
 *   Headers (FVDOC.H) are records of the same file, added and changed with  *
 *   the same dialogs; only their property list differs.                     *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "filexfer.h"
#include "FVSET.H"

/* Extensions an uploaded document may keep.  Anything else is stored as    */
/* .TXT - the folder only ever holds documents to show, never programs.     */
static const CHAR *const okext[] = {
     "TXT", "ANS", "ASC", "NFO", "DIZ", "IBM", "DOC", "LOG", "MD", NULL
};

/* Properties.  Each kind of record lists its own, in the order the user    */
/* sees them and numbered by that position.  P_FILE stays last in each      */
/* list: it is the one item only sysops see.                                */
enum { P_TITLE = 1, P_KEY, P_MODE, P_TYPE, P_BAUD, P_HIDDEN, P_QUIET,
       P_RETURN, P_HEADER, P_ROWS, P_FILE };

static const INT docprops[] = {
     P_TITLE, P_KEY, P_MODE, P_TYPE, P_BAUD, P_HIDDEN, P_QUIET, P_RETURN,
     P_HEADER, P_FILE, 0
};
static const INT hdrprops[] = {
     P_TITLE, P_TYPE, P_ROWS, P_FILE, 0
};

static const INT *
props(VOID)                          /* the working copy's property list     */
{
     return fvv->doc.kind == FVK_HEADER ? hdrprops : docprops;
}

static INT
nprops(VOID)                         /* how many this user may change        */
{
     const INT *p = props();
     INT n;

     for (n = 0 ; p[n] != 0 ; n++) {
     }
     return lib_issysop() ? n : n - 1;     /* only sysops link files       */
}

static GBOOL
mayedit(const struct fvdoc *d)       /* may this user change a document?     */
{
     return lib_issysop() || (lib_canadd() && sameas(d->owner, usaptr->userid));
}

static VOID
folderpath(                          /* a file in the document folder        */
const CHAR *name,
CHAR *buf,
INT size)
{
     struct fvdoc tmp;

     setmem(&tmp, sizeof(tmp), 0);
     stzcpy(tmp.file, name, sizeof(tmp.file));
     doc_path(&tmp, buf, size);
}

static VOID
freshviews(VOID)                     /* keep the stored view count           */
{
     struct fvdoc cur;

     /* The working copy is older than the record whenever the document     */
     /* was viewed meanwhile (V in the property menu); never overwrite      */
     /* the count with that stale value.                                    */
     if (!fvv->isnew && doc_get(fvv->doc.name, &cur)) {
          fvv->doc.views = cur.views;
     }
}

static GBOOL
savedoc(VOID)                        /* insert or update the working copy    */
{
     freshviews();
     fvv->doc.changed = today();
     if (fvv->isnew) {
          if (!doc_insert(&fvv->doc)) {
               tv_set(TV_DOC, fvv->doc.name);
               prfmsg(NAMETAKN);
               return FALSE;
          }
          fvv->isnew = FALSE;
          shocst("GALFV DOCUMENT ADDED", "%s added %s",
                 usaptr->userid, fvv->doc.name);
          return TRUE;
     }
     return doc_update(&fvv->doc);
}

static VOID
dropold(const CHAR *newfile)         /* content replaced: remove old file    */
{
     CHAR path[FV_FULSIZ];

     /* Only files we own (in the document folder), never a linked file,    */
     /* and never the file we just wrote.                                   */
     if (!fvv->isnew && !fvv->doc.linked && fvv->doc.file[0] != '\0'
      && !sameas(fvv->doc.file, newfile)) {
          doc_path(&fvv->doc, path, sizeof(path));
          unlink(path);
     }
}

/* ------------------------------------------------------------------------ *
 * Upload.  The engine's file transfer takes over the user (state ftfstt)   *
 * and calls fvupl() at each step (WG33 FILEXFER.C fileup, ftfrop, ftfrcl). *
 * The file is received under a temporary name and only renamed into place *
 * once it has arrived complete, so a failed upload never damages a        *
 * document's existing content.                                             *
 * ------------------------------------------------------------------------ */

static VOID
upname(const CHAR *sent, CHAR *out)  /* NAME.EXT for an uploaded file        */
{
     CHAR ext[8], *p;
     INT i;

     fileparts(GCPART_EXTN, sent, ext, sizeof(ext));
     p = (ext[0] == '.') ? ext + 1 : ext;
     strupr(p);
     for (i = 0 ; okext[i] != NULL ; i++) {
          if (sameas(p, okext[i])) {
               break;
          }
     }
     sprintf(out, "%s.%s", fvv->doc.name, okext[i] != NULL ? okext[i] : "TXT");
}

static INT
fvupl(INT fupcod)                    /* the file transfer's callbacks        */
{
     struct fvvda *vp = (struct fvvda *)vdaoff(usrnum);

     switch (fupcod) {
     case FUPPTH:                    /* "would this file overwrite one?"     */
          return 0;                  /*   no: always a fresh temporary file  */
     case FUPBEG:                    /* a file is about to arrive            */
          if (vp->upok) {
               strcpy(ftfbuf, "Only one file can be uploaded for a document.");
               return 0;
          }
          upname(ftfscb->fname, vp->upnew);
          sprintf(ftfbuf, "%s.UP$", vp->doc.name);
          folderpath(ftfbuf, vp->uptmp, sizeof(vp->uptmp));
          strcpy(ftfbuf, vp->uptmp);
          ftfscb->maxbyt = fvcfg.maxkb * 1024L;
          return 1;
     case FUPEND:                    /* it arrived complete                  */
          {
               CHAR path[FV_FULSIZ];

               folderpath(vp->upnew, path, sizeof(path));
               unlink(path);
               if (rename(vp->uptmp, path) == 0) {
                    vp->upok = TRUE;
                    strcpy(ftfbuf, path);  /* let the engine set its date    */
               }
               else {
                    unlink(vp->uptmp);
                    ftfbuf[0] = '\0';
               }
          }
          return 0;
     case FUPSKP:                    /* it was aborted part way              */
          unlink(vp->uptmp);
          return 0;
     case FUPFIN:                    /* the transfer session is over         */
          usrptr->state = fvstt;
          setmbk(fvmb);
          if (vp->upok) {
               dropold(vp->upnew);
               stzcpy(vp->doc.file, vp->upnew, sizeof(vp->doc.file));
               vp->doc.linked = 0;
               if (savedoc()) {
                    tv_set(TV_DOC, vp->doc.name);
                    prfmsg(UPLDONE);
                    ed_menu();
               }
               else {
                    lib_home();
               }
          }
          else {
               prfmsg(UPLFAIL);
               if (vp->isnew) {      /* still nothing to show: ask again     */
                    prfmsg(lib_issysop() ? ADDSRCS : ADDSRC);
                    usrptr->substt = ST_ADDSRC;
               }
               else {
                    ed_menu();
               }
          }
          fv_out();
          rstmbk();
          return 1;                  /* 1: we have taken the user back       */
     case FUPHUP:                    /* hung up mid-transfer                 */
          unlink(vp->uptmp);
          return 0;
     }
     return 0;
}

static VOID
startupload(VOID)
{
     fvv->upok = FALSE;
     fvv->uptmp[0] = '\0';
     usrptr->substt = ST_UPLOAD;
     /* No proposed name: the engine then offers the batch protocols        */
     /* (ZMODEM, YMODEM...), which send the file's own name, so upname()    */
     /* can keep its extension.  With a name it offers only the single-file */
     /* protocols, which send none, and every upload would become .TXT      */
     /* (WG33 FILEXFER.C fileup, fupask).  FUPBEG refuses a second file.    */
     fileup("", "", fvupl);          /* "" protocol: the user picks one      */
}

/* ------------------------------------------------------------------------ *
 * Write online.  The editor keeps lines separated by '\r'; files on disk   *
 * use CR LF.                                                               *
 * ------------------------------------------------------------------------ */

static VOID
loadtext(VOID)                       /* existing text into the editor buffer */
{
     CHAR path[FV_FULSIZ];
     FILE *fp;
     INT c, n = 0;

     setmem(fvv->u.edt, sizeof(fvv->u.edt), 0);
     if (fvv->isnew || fvv->doc.linked || fvv->doc.file[0] == '\0') {
          return;
     }
     doc_path(&fvv->doc, path, sizeof(path));
     if ((fp = fopen(path, "rb")) == NULL) {
          return;
     }
     while ((c = getc(fp)) != EOF && c != 0x1A && n < FV_EDTSIZ - 2) {
          if (c == '\n') {
               if (n == 0 || fvv->u.edt[n - 1] != '\r') {
                    fvv->u.edt[n++] = '\r';
               }
          }
          else if (c != '\0') {
               fvv->u.edt[n++] = (CHAR)c;
          }
     }
     fclose(fp);
}

static GBOOL
textfits(VOID)                       /* small enough to edit online?         */
{
     CHAR path[FV_FULSIZ];
     FILE *fp;
     LONG len;

     if (fvv->isnew || fvv->doc.file[0] == '\0') {
          return TRUE;
     }
     if (fvv->doc.linked) {
          return FALSE;              /* linked files are not ours to rewrite */
     }
     doc_path(&fvv->doc, path, sizeof(path));
     if ((fp = fopen(path, "rb")) == NULL) {
          return TRUE;
     }
     fseek(fp, 0L, SEEK_END);
     len = ftell(fp);
     fclose(fp);
     return len < FV_EDTSIZ - 512;   /* room for CR LF -> CR and some typing */
}

static SHORT
edone(                               /* the editor has finished              */
SHORT flags)                         /*   ED_QUITEX if the user quit         */
{
     CHAR name[FV_NAMSIZ + 8], path[FV_FULSIZ];
     FILE *fp;
     CHAR *s;

     usrptr->state = fvstt;          /* back from the editor to us           */
     setmbk(fvmb);
     unpad(fvv->u.edt);
     if ((flags & ED_QUITEX) || fvv->u.edt[0] == '\0') {
          prfmsg(EDTQUIT);
          if (fvv->isnew) {
               prfmsg(lib_issysop() ? ADDSRCS : ADDSRC);
               usrptr->substt = ST_ADDSRC;
          }
          else {
               ed_menu();
          }
     }
     else {
          sprintf(name, "%s.TXT", fvv->doc.name);
          folderpath(name, path, sizeof(path));
          if ((fp = fopen(path, "wb")) == NULL) {
               tv_set(TV_FILE, path);
               prfmsg(EDTWERR);
               ed_menu();
          }
          else {
               for (s = fvv->u.edt ; *s != '\0' ; s++) {
                    if (*s == '\r') {
                         fputs("\r\n", fp);
                    }
                    else {
                         fputc(*s, fp);
                    }
               }
               fputs("\r\n", fp);
               fclose(fp);
               dropold(name);
               stzcpy(fvv->doc.file, name, sizeof(fvv->doc.file));
               fvv->doc.linked = 0;
               if (savedoc()) {
                    tv_set(TV_DOC, fvv->doc.name);
                    prfmsg(EDTSAVED);
                    ed_menu();
               }
               else {
                    unlink(path);    /* the name was taken: undo             */
                    lib_home();
               }
          }
     }
     fv_out();
     rstmbk();
     return 1;                       /* 1: leave the editor                  */
}

static VOID
startwrite(VOID)
{
     if (!textfits()) {
          prfmsg(EDTBIG);
          ed_menu();
          return;
     }
     loadtext();
     bgnedt(FV_EDTSIZ, fvv->u.edt, 0, NULL, edone, 0);
}

/* ------------------------------------------------------------------------ *
 * The property menu.                                                       *
 * ------------------------------------------------------------------------ */

static const CHAR *
plabel(INT item)
{
     switch (item) {
     case P_TITLE:  return "Title";
     case P_KEY:    return "Key needed to view it";
     case P_MODE:   return "Display mode";
     case P_TYPE:   return "Content type";
     case P_BAUD:   return "Simulated baud rate";
     case P_HIDDEN: return "Hidden from the list";
     case P_QUIET:  return "Continuous messages";
     case P_RETURN: return "When finished";
     case P_HEADER: return "Header";
     case P_ROWS:   return "Height in lines";
     default:       return "File";
     }
}

static const CHAR *
pvalue(INT item)
{
     const struct fvdoc *d = &fvv->doc;
     static CHAR buf[8];

     switch (item) {
     case P_TITLE:  return d->title[0] != '\0' ? d->title : "(none)";
     case P_KEY:    return d->key[0] != '\0' ? d->key : "(anyone)";
     case P_MODE:   return fv_modename(d->mode);
     case P_TYPE:   return fv_typename(d->type);
     case P_BAUD:   return fv_baudname(d->baud);
     case P_HIDDEN: return d->hidden ? "YES" : "NO";
     case P_QUIET:  return d->quiet ? "HIDE" : "SHOW";
     case P_RETURN: return d->autoret ? "RETURN" : "WAIT";
     case P_HEADER: return d->header[0] != '\0' ? d->header : "NONE";
     case P_ROWS:
          if (d->rows <= 0) {
               return "AUTO";
          }
          sprintf(buf, "%d", d->rows);
          return buf;
     default:       return d->file[0] != '\0' ? d->file : "(none)";
     }
}

static const CHAR *
plimit(INT item)
{
     static CHAR buf[96];

     switch (item) {
     case P_TITLE:
          sprintf(buf, "text, up to %d characters", FV_TITSIZ - 1);
          return buf;
     case P_KEY:
          return "a key name (a single - lets anyone view it)";
     case P_MODE:
          return "DEFAULT, PAGED, CONTINUOUS or ASK";
     case P_TYPE:
          return "AUTO, TEXT, ANSI or IBM";
     case P_BAUD:
          sprintf(buf, "DEFAULT, FULL, or a rate from %ld to %ld",
                  FV_MINBAUD, FV_MAXBAUD);
          return buf;
     case P_HIDDEN:
          return "YES or NO (hidden documents open only by name or link)";
     case P_QUIET:
          return "SHOW or HIDE the header and \"Stopped\" line around a "
                 "continuous display";
     case P_RETURN:
          return "WAIT for Enter at the end, or RETURN straight to where "
                 "the caller started";
     case P_HEADER:
          return "NONE, or the name of a header (H at the library lists "
                 "them)";
     case P_ROWS:
          sprintf(buf, "AUTO (count its lines), or 1 to %d for ANSI art "
                  "that moves the cursor", FV_MAXHROWS);
          return buf;
     default:
          return "the full path of a file on the server";
     }
}

static INT
usedby(const CHAR *name)             /* documents that show a header         */
{
     struct fvdoc d;
     CHAR last[FV_NAMSIZ];
     INT n = 0;

     last[0] = '\0';
     while (doc_next(last, &d)) {
          stzcpy(last, d.name, sizeof(last));
          if (d.kind == FVK_DOC && sameas(d.header, name)) {
               n++;
          }
     }
     return n;
}

VOID
ed_menu(VOID)                        /* show a document's properties         */
{
     const INT *p = props();
     INT i, n = nprops();

     tv_set(TV_DOC, fvv->doc.name);
     tv_set(TV_OWNER, fvv->doc.owner);
     tv_set(TV_VALUE, fvv->doc.created ? ncdate(fvv->doc.created) : "-");
     if (fvv->doc.kind == FVK_HEADER) {
          tv_setn(TV_COUNT, usedby(fvv->doc.name));
          prfmsg(HDRMHDR);
     }
     else {
          tv_setn(TV_COUNT, fvv->doc.views);
          prfmsg(DOCHDR);
     }
     for (i = 0 ; i < n ; i++) {
          tv_setn(TV_ITEM, i + 1);
          tv_set(TV_LABEL, plabel(p[i]));
          tv_set(TV_VALUE, pvalue(p[i]));
          prfmsg(DOCROW);
     }
     prfmsg(DOCPRMT);
     usrptr->substt = ST_DOCMENU;
}

static GBOOL                         /*   FALSE if the value was not allowed */
pset(INT item, const CHAR *text)     /* apply a new property value           */
{
     struct fvdoc *d = &fvv->doc;
     struct fvdoc h;
     INT n;
     LONG b;

     switch (item) {
     case P_TITLE:
          stzcpy(d->title, text, sizeof(d->title));
          return TRUE;
     case P_KEY:
          if (sameas(text, "-")) {
               text = "";
          }
          if (!fv_goodkey(text)) {
               return FALSE;
          }
          stzcpy(d->key, text, sizeof(d->key));
          return TRUE;
     case P_MODE:
          if ((n = fv_parsemode(text, TRUE)) < 0) {
               return FALSE;
          }
          d->mode = (SHORT)n;
          return TRUE;
     case P_TYPE:
          if ((n = fv_parsetype(text)) < 0) {
               return FALSE;
          }
          d->type = (SHORT)n;
          return TRUE;
     case P_BAUD:
          if ((b = fv_parsebaud(text, TRUE)) < -1) {
               return FALSE;
          }
          d->baud = b;
          return TRUE;
     case P_HIDDEN:
          if (toupper((UCHAR)*text) != 'Y' && toupper((UCHAR)*text) != 'N') {
               return FALSE;
          }
          d->hidden = (toupper((UCHAR)*text) == 'Y');
          return TRUE;
     case P_QUIET:
          if (sameto(text, "SHOW")) {
               d->quiet = 0;
          }
          else if (sameto(text, "HIDE")) {
               d->quiet = 1;
          }
          else {
               return FALSE;
          }
          return TRUE;
     case P_RETURN:
          if (sameto(text, "WAIT")) {
               d->autoret = 0;
          }
          else if (sameto(text, "RETURN")) {
               d->autoret = 1;
          }
          else {
               return FALSE;
          }
          return TRUE;
     case P_HEADER:
          if (sameas(text, "NONE") || sameas(text, "-")) {
               d->header[0] = '\0';
               return TRUE;
          }
          if (!doc_get(text, &h) || h.kind != FVK_HEADER) {
               return FALSE;
          }
          stzcpy(d->header, h.name, sizeof(d->header));
          return TRUE;
     case P_ROWS:
          if (sameto(text, "AUTO")) {
               d->rows = 0;
               return TRUE;
          }
          if (!isdigit((UCHAR)*text) || (n = atoi(text)) < 1
           || n > FV_MAXHROWS) {
               return FALSE;
          }
          d->rows = (SHORT)n;
          return TRUE;
     default:                       /* P_FILE: sysops link a server file    */
          if (!isfile(text) || strlen(text) >= sizeof(d->file)) {
               return FALSE;
          }
          dropold("");               /* our own copy is no longer used       */
          stzcpy(d->file, text, sizeof(d->file));
          d->linked = 1;
          shocst("GALFV DOCUMENT LINKED", "%s linked %s to %.40s",
                 usaptr->userid, d->name, text);
          return TRUE;
     }
}

/* ------------------------------------------------------------------------ *
 * Adding.                                                                  *
 * ------------------------------------------------------------------------ */

static VOID
askname(VOID)                        /* name for the new document or header  */
{
     prfmsg(fvv->doc.kind == FVK_HEADER ? HDRNAME : ADDNAME);
     usrptr->substt = ST_ADDNAME;
}

VOID
ed_add(                              /* start adding a document or header    */
const CHAR *name,                    /*   its name, or "" to ask             */
SHORT kind)                          /*   FVK_xxx                            */
{
     struct fvdoc old;

     setmem(&fvv->doc, sizeof(fvv->doc), 0);
     fvv->doc.kind = kind;
     fvv->isnew = TRUE;
     stzcpy(fvv->doc.owner, usaptr->userid, sizeof(fvv->doc.owner));
     fvv->doc.created = today();
     fvv->doc.mode = FVM_DEFAULT;
     fvv->doc.type = FVY_AUTO;
     if (doc_goodname(name) && !doc_get(name, &old)) {
          stzcpy(fvv->doc.name, name, sizeof(fvv->doc.name));
          strupr(fvv->doc.name);
          tv_set(TV_DOC, fvv->doc.name);
          prfmsg(ADDTITLE);
          usrptr->substt = ST_ADDTITLE;
          return;
     }
     askname();
}

GBOOL
ed_pick(                             /* change or delete a record by name    */
const CHAR *name,
GBOOL del)                           /*   TRUE: ask to delete it             */
{
     struct fvdoc d;

     if (!doc_get(name, &d)) {
          tv_set(TV_DOC, name);
          prfmsg(NODOC);
          return lib_home();
     }
     if (!mayedit(&d)) {
          tv_set(TV_DOC, d.name);
          prfmsg(NOTYOURS);
          return lib_home();
     }
     fvv->doc = d;
     fvv->isnew = FALSE;
     if (del) {
          tv_set(TV_DOC, d.name);
          tv_set(TV_TITLE, d.title);
          prfmsg(DELCONF);
          usrptr->substt = ST_DELCONF;
     }
     else {
          ed_menu();
     }
     return TRUE;
}

static VOID
restofline(CHAR *buf, INT size)      /* the whole input line, spaces and all */
{
     rstrin();
     stzcpy(buf, skpwht(input), size);
     unpad(buf);
}

/* ------------------------------------------------------------------------ *
 * Input.                                                                   *
 * ------------------------------------------------------------------------ */

GBOOL
ed_input(VOID)                       /* input in the add/edit/delete dialogs */
{
     struct fvdoc d;
     CHAR line[INPSIZ], cmd;
     INT n;

     switch (usrptr->substt) {
     case ST_ADDNAME:
          if (margc == 0) {
               return lib_home();
          }
          if (!doc_goodname(margv[0])) {
               prfmsg(BADNAME);
               askname();
               return TRUE;
          }
          if (doc_get(margv[0], &d)) {
               tv_set(TV_DOC, d.name);
               prfmsg(NAMETAKN);
               askname();
               return TRUE;
          }
          ed_add(margv[0], fvv->doc.kind);
          return TRUE;
     case ST_ADDTITLE:
          restofline(line, sizeof(line));
          stzcpy(fvv->doc.title, line[0] != '\0' ? line : fvv->doc.name,
                 sizeof(fvv->doc.title));
          prfmsg(lib_issysop() ? ADDSRCS : ADDSRC);
          usrptr->substt = ST_ADDSRC;
          return TRUE;
     case ST_ADDSRC:
     case ST_REPLSRC:
          cmd = margc > 0 ? (CHAR)toupper((UCHAR)margv[0][0]) : 'X';
          if (cmd == 'U') {
               startupload();
          }
          else if (cmd == 'W') {
               startwrite();
          }
          else if (cmd == 'L' && lib_issysop()) {
               prfmsg(ADDPATH);
               usrptr->substt = ST_ADDPATH;
          }
          else if (fvv->isnew) {
               prfmsg(ADDCANC);
               return lib_home();
          }
          else {
               ed_menu();
          }
          return TRUE;
     case ST_ADDPATH:
          restofline(line, sizeof(line));
          if (line[0] == '\0' || !pset(P_FILE, line)) {
               tv_set(TV_FILE, line);
               prfmsg(NOOPEN);
               prfmsg(lib_issysop() ? ADDSRCS : ADDSRC);
               usrptr->substt = ST_ADDSRC;
               return TRUE;
          }
          if (savedoc()) {
               ed_menu();
               return TRUE;
          }
          return lib_home();
     case ST_DOCSEL:
     case ST_DOCDEL:
          if (margc == 0) {
               return lib_home();
          }
          return ed_pick(margv[0], usrptr->substt == ST_DOCDEL);
     case ST_DELCONF:
          if (margc > 0 && toupper((UCHAR)margv[0][0]) == 'Y') {
               fvv->isnew = FALSE;
               dropold("");          /* our own copy of the file goes too    */
               doc_delete(fvv->doc.name);
               shocst("GALFV DOCUMENT DELETED", "%s deleted %s",
                      usaptr->userid, fvv->doc.name);
               tv_set(TV_DOC, fvv->doc.name);
               prfmsg(DELDONE);
          }
          return lib_home();
     case ST_DOCMENU:
          if (margc == 0) {
               ed_menu();
               return TRUE;
          }
          if (isdigit((UCHAR)margv[0][0])) {
               n = atoi(margv[0]);
               if (n >= 1 && n <= nprops()) {
                    n = props()[n - 1];
                    fvv->item = n;
                    tv_set(TV_LABEL, plabel(n));
                    tv_set(TV_VALUE, pvalue(n));
                    tv_set(TV_LIMIT, plimit(n));
                    prfmsg(DOCVAL);
                    usrptr->substt = ST_DOCVAL;
                    return TRUE;
               }
          }
          switch (toupper((UCHAR)margv[0][0])) {
          case 'U':
               startupload();
               return TRUE;
          case 'W':
               startwrite();
               return TRUE;
          case 'V':
               return lib_opendoc(&fvv->doc, FVBACK_DOCMENU);
          case 'X':
          case 'Q':
               return lib_home();
          }
          ed_menu();
          return TRUE;
     case ST_DOCVAL:
          restofline(line, sizeof(line));
          if (line[0] != '\0') {
               if (pset(fvv->item, line)) {
                    savedoc();
                    tv_set(TV_LABEL, plabel(fvv->item));
                    tv_set(TV_VALUE, pvalue(fvv->item));
                    prfmsg(DOCSAVED);
               }
               else {
                    prfmsg(BADVAL);
               }
          }
          ed_menu();
          return TRUE;
     }
     return lib_home();
}

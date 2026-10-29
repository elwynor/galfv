/* Build 26.10.21.2 10:40AM */
/*****************************************************************************
 *   FVLIB.C                                   File Viewer - Library prompt   *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   What happens when a user arrives, and the document library they see    *
 *   if they did not come for one particular document.                       *
 *                                                                           *
 *   A menu page reaches us through the engine's entmdl(), which leaves      *
 *   "<select character><command string> <anything typed after it>" in      *
 *   input[] (WG33 MENUING.C entmdl).  So a module page whose command        *
 *   string is a document name is a direct link to that document, and a     *
 *   user can also type the name after the menu selection.  If a link names *
 *   a document that does not exist yet and the user may add documents, we   *
 *   offer to create it there and then.                                      *
 *                                                                           *
 *   Authors also reach the header section (H) from here: headers are kept  *
 *   with the documents but never listed or opened as documents.            *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

/* ------------------------------------------------------------------------ *
 * Who may do what.                                                         *
 * ------------------------------------------------------------------------ */

GBOOL
lib_issysop(VOID)                    /* holds the sysop key?                 */
{
     return haskey(fvcfg.cfgkey);
}

GBOOL
lib_canadd(VOID)                     /* may add (and manage own) documents?  */
{
     return lib_issysop() || fvcfg.addkey[0] == '\0' || haskey(fvcfg.addkey);
}

GBOOL
lib_canview(const struct fvdoc *d)   /* may this user open a document?       */
{
     CHAR key[KEYSIZ];

     if (d->key[0] == '\0' || lib_issysop()) {
          return TRUE;
     }
     stzcpy(key, d->key, sizeof(key));         /* haskey() wants writable    */
     return haskey(key);
}

static GBOOL
listed(const struct fvdoc *d)        /* does it appear in the list?          */
{
     return d->kind == FVK_DOC && lib_canview(d)
         && (!d->hidden || lib_issysop());
}

/* ------------------------------------------------------------------------ *
 * The list.  Numbers are positions among the documents this user can see,  *
 * so they are worked out afresh each time.                                 *
 * ------------------------------------------------------------------------ */

static GBOOL
nth(INT want, struct fvdoc *d)       /* the want'th listed document (1-based)*/
{
     CHAR last[FV_NAMSIZ];
     INT n = 0;

     last[0] = '\0';
     while (doc_next(last, d)) {
          stzcpy(last, d->name, sizeof(last));
          if (listed(d) && ++n == want) {
               return TRUE;
          }
     }
     return FALSE;
}

static VOID
showlist(VOID)                       /* one page of the document list        */
{
     struct fvdoc d;
     CHAR last[FV_NAMSIZ], title[FV_TITSIZ];
     INT n = 0, total = 0, shown = 0;

     last[0] = '\0';                 /* count first, so paging can wrap      */
     while (doc_next(last, &d)) {
          stzcpy(last, d.name, sizeof(last));
          if (listed(&d)) {
               total++;
          }
     }
     if (fvv->listofs >= total || fvv->listofs < 0) {
          fvv->listofs = 0;          /* past the last page: back to the top  */
     }
     prfmsg(LISTHDR);
     last[0] = '\0';
     while (shown < FV_LISTPG && doc_next(last, &d)) {
          stzcpy(last, d.name, sizeof(last));
          if (!listed(&d) || ++n <= fvv->listofs) {
               continue;
          }
          /* Keep each row inside 78 columns: the BBS wraps lines that      */
          /* reach the screen edge.                                         */
          stzcpy(title, d.title, d.hidden ? 46 : 55);
          tv_setn(TV_NUM, n);
          tv_set(TV_DOC, d.name);
          tv_set(TV_TITLE, title);
          prfmsg(d.hidden ? LISTROWH : LISTROW);
          shown++;
     }
     if (total == 0) {
          prfmsg(LISTNONE);
     }
     else if (fvv->listofs + shown < total) {
          prfmsg(LISTMORE);
     }
}

VOID
lib_prompt(VOID)                     /* the library prompt for this user     */
{
     prfmsg(lib_issysop() ? LIBPRS : (lib_canadd() ? LIBPRA : LIBPRU));
     usrptr->substt = ST_LIBRARY;
}

static VOID
library(VOID)                        /* list from the top, then the prompt   */
{
     fvv->listofs = 0;
     showlist();
     lib_prompt();
}

/* ------------------------------------------------------------------------ *
 * Opening documents.                                                       *
 * ------------------------------------------------------------------------ */

GBOOL
lib_opendoc(                         /* view a library document              */
struct fvdoc *d,
SHORT back)                          /*   FVBACK_DOCMENU, or home            */
{
     CHAR path[FV_PTHSIZ + FVS_DIRSIZ + 2];
     struct fvhead hd, *hp = NULL;
     struct fvdoc h;
     SHORT mode;
     LONG baud;

     if (!lib_canview(d)) {
          tv_set(TV_DOC, d->name);
          prfmsg(NOACCESS);
          return lib_home();
     }
     doc_path(d, path, sizeof(path));
     mode = (d->mode == FVM_DEFAULT) ? fvcfg.defmode : d->mode;
     baud = (d->baud == 0) ? fvcfg.defbaud : (d->baud < 0 ? 0L : d->baud);
     stzcpy(fvv->docnam, d->name, sizeof(fvv->docnam));
     if (d->header[0] != '\0' && doc_get(d->header, &h)
      && h.kind == FVK_HEADER) {    /* (a deleted header: just none)        */
          doc_path(&h, hd.path, sizeof(hd.path));
          hd.type = h.type;
          hd.rows = h.rows;
          hp = &hd;
     }
     if (!vw_open(path, d->title, d->type, mode, baud, back,
                  (SHORT)((d->quiet ? FVO_QUIET : 0)
                        | (d->autoret ? FVO_RETURN : 0)), hp)) {
          return back == FVBACK_DOCMENU ? (ed_menu(), TRUE) : lib_home();
     }
     doc_counted(d->name);
     return TRUE;
}

static GBOOL
openraw(const CHAR *path)            /* sysop: view any server file          */
{
     shocst("GALFV SERVER FILE VIEWED", "%s viewed %s", usaptr->userid, path);
     if (!vw_open(path, "", FVY_AUTO,
                  fvcfg.defmode == FVM_ASK ? FVM_PAGED : fvcfg.defmode,
                  fvcfg.defbaud, FVBACK_LIBRARY, 0, NULL)) {
          return lib_home();
     }
     return TRUE;
}

GBOOL
lib_back(VOID)                       /* the viewer has finished              */
{
     if (fvv->back == FVBACK_DOCMENU) {
          ed_menu();
          return TRUE;
     }
     return lib_home();
}

GBOOL
lib_home(VOID)                       /* finished: where to now?              */
{
     if (fvv->home == FVBACK_EXIT) {
          return FALSE;              /* came by a direct link: back to menu  */
     }
     if (fvv->inhdrs) {
          lib_headers();
     }
     else {
          library();
     }
     return TRUE;
}

/* ------------------------------------------------------------------------ *
 * The header section.  Headers are few, so they are listed in one go.      *
 * ------------------------------------------------------------------------ */

VOID
lib_headers(VOID)                    /* list the headers, then the prompt    */
{
     struct fvdoc d;
     CHAR last[FV_NAMSIZ], title[FV_TITSIZ], rows[8];
     INT n = 0;

     fvv->inhdrs = TRUE;
     prfmsg(HDRLIST);
     last[0] = '\0';
     while (doc_next(last, &d)) {
          stzcpy(last, d.name, sizeof(last));
          if (d.kind != FVK_HEADER) {
               continue;
          }
          stzcpy(title, d.title, 46);     /* keep the row inside 78 columns  */
          if (d.rows > 0) {
               sprintf(rows, "%d", d.rows);
          }
          else {
               stzcpy(rows, "auto", sizeof(rows));
          }
          tv_set(TV_DOC, d.name);
          tv_set(TV_VALUE, rows);
          tv_set(TV_TITLE, title);
          prfmsg(HDRROW);
          n++;
     }
     if (n == 0) {
          prfmsg(HDRNONE);
     }
     prfmsg(HDRPRMT);
     usrptr->substt = ST_HDRLIST;
}

static GBOOL
hdrcmd(VOID)                         /* a line typed at the header prompt    */
{
     struct fvdoc d;
     CHAR cmd;

     cmd = (margc > 0 && margv[0][1] == '\0')
         ? (CHAR)toupper((UCHAR)margv[0][0]) : '\0';
     if (margc == 0 || cmd == 'X' || cmd == 'Q') {
          fvv->inhdrs = FALSE;       /* back to the document library         */
          library();
          return TRUE;
     }
     switch (cmd) {
     case '\0':                      /* a header name: change it             */
          if (doc_get(margv[0], &d) && d.kind == FVK_HEADER) {
               return ed_pick(margv[0], FALSE);
          }
          tv_set(TV_DOC, margv[0]);
          prfmsg(NOHDR);
          prfmsg(HDRPRMT);
          return TRUE;
     case 'A':
          ed_add(margc > 1 ? margv[1] : "", FVK_HEADER);
          return TRUE;
     case 'D':
          if (margc > 1) {
               return ed_pick(margv[1], TRUE);
          }
          prfmsg(HDRDELW);
          usrptr->substt = ST_DOCDEL;
          return TRUE;
     default:                        /* L, or anything else: list again      */
          lib_headers();
          return TRUE;
     }
}

/* ------------------------------------------------------------------------ *
 * Arriving.                                                                *
 * ------------------------------------------------------------------------ */

GBOOL
lib_entry(VOID)                      /* first call, from the menu            */
{
     CHAR arg[INPSIZ], word[FV_NAMSIZ + 1];
     struct fvdoc d;
     GBOOL found;
     INT i;

     /* entmdl() has run input through parsin(); put the spaces back and     */
     /* skip the menu selection character to get the command string.        */
     rstrin();
     stzcpy(arg, input[0] != '\0' ? skpwht(input + 1) : "", sizeof(arg));
     unpad(arg);
     fvv->home = FVBACK_LIBRARY;
     fvv->inhdrs = FALSE;
     if (arg[0] == '\0') {
          prfmsg(WELCOME);
          library();
          return TRUE;
     }
     for (i = 0 ; arg[i] != '\0' && arg[i] != ' ' && i < FV_NAMSIZ ; i++) {
          word[i] = arg[i];
     }
     word[i] = '\0';
     found = doc_get(word, &d);      /* (a header's name is not a document)  */
     if (found && d.kind == FVK_DOC) {    /* a document: show it, then leave */
          fvv->home = FVBACK_EXIT;
          return lib_opendoc(&d, FVBACK_EXIT);
     }
     if (!found && doc_goodname(word) && lib_canadd()) {
          /* A link to a document that does not exist yet: offer to make it  */
          /* now ("create documents on the fly").                            */
          fvv->home = FVBACK_EXIT;
          stzcpy(fvv->docnam, word, sizeof(fvv->docnam));
          strupr(fvv->docnam);
          tv_set(TV_DOC, fvv->docnam);
          prfmsg(CREATEQ);
          usrptr->substt = ST_CREATEQ;
          return TRUE;
     }
     if (lib_issysop() && fvcfg.rawpath && isfile(arg)) {
          fvv->home = FVBACK_EXIT;   /* a server file by path (sysops only)  */
          return openraw(arg);
     }
     tv_set(TV_DOC, word);
     prfmsg(NODOC);
     library();
     return TRUE;
}

/* ------------------------------------------------------------------------ *
 * Input at the library and its small dialogs.                              *
 * ------------------------------------------------------------------------ */

static GBOOL
libcmd(VOID)                         /* a line typed at the library prompt   */
{
     struct fvdoc d;
     CHAR cmd;

     if (margc == 0) {               /* Enter: the next page of the list     */
          fvv->listofs += FV_LISTPG;
          showlist();
          lib_prompt();
          return TRUE;
     }
     if (isdigit((UCHAR)margv[0][0])) {
          if (nth(atoi(margv[0]), &d)) {
               return lib_opendoc(&d, FVBACK_LIBRARY);
          }
          prfmsg(BADNUM);
          lib_prompt();
          return TRUE;
     }
     if (margv[0][1] == '\0') {      /* a one-letter command                 */
          cmd = (CHAR)toupper((UCHAR)margv[0][0]);
          switch (cmd) {
          case 'L':
          case '?':
               library();
               return TRUE;
          case 'X':
          case 'Q':
               return FALSE;
          case 'A':
               if (lib_canadd()) {
                    ed_add(margc > 1 ? margv[1] : "", FVK_DOC);
                    return TRUE;
               }
               break;
          case 'H':
               if (lib_canadd()) {
                    lib_headers();
                    return TRUE;
               }
               break;
          case 'E':
          case 'D':
               if (lib_canadd()) {
                    prfmsg(cmd == 'E' ? EDWHICH : DELWHICH);
                    usrptr->substt = (cmd == 'E') ? ST_DOCSEL : ST_DOCDEL;
                    return TRUE;
               }
               break;
          case 'S':
               if (lib_issysop()) {
                    sy_settings();
                    return TRUE;
               }
               break;
          case 'F':
               if (lib_issysop() && fvcfg.rawpath) {
                    prfmsg(RAWASK);
                    usrptr->substt = ST_RAWPATH;
                    return TRUE;
               }
               break;
          }
          prfmsg(LIBHELP);
          lib_prompt();
          return TRUE;
     }
     if (doc_get(margv[0], &d) && d.kind == FVK_DOC) {   /* a document name */
          return lib_opendoc(&d, FVBACK_LIBRARY);
     }
     tv_set(TV_DOC, margv[0]);
     prfmsg(NODOC);
     lib_prompt();
     return TRUE;
}

GBOOL
lib_input(VOID)                      /* input at the library and its dialogs */
{
     CHAR path[INPSIZ];

     switch (usrptr->substt) {
     case ST_CREATEQ:                /* "NAME does not exist. Create it?"   */
          if (margc > 0 && toupper((UCHAR)margv[0][0]) == 'Y') {
               ed_add(fvv->docnam, FVK_DOC);
               return TRUE;
          }
          return lib_home();
     case ST_HDRLIST:                /* at the header section prompt         */
          return hdrcmd();
     case ST_RAWPATH:                /* sysop: a server file to view        */
          rstrin();
          stzcpy(path, skpwht(input), sizeof(path));
          unpad(path);
          if (path[0] == '\0') {
               lib_prompt();
               return TRUE;
          }
          if (!isfile(path)) {
               tv_set(TV_FILE, path);
               prfmsg(NOOPEN);
               lib_prompt();
               return TRUE;
          }
          return openraw(path);
     default:
          return libcmd();
     }
}

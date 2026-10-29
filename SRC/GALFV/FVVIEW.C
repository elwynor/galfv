/* Build 26.10.27.1 08:06PM */
/*****************************************************************************
 *   FVVIEW.C                                         File Viewer - Viewer    *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *   Originally (C) Copyright 1995 High Velocity Software, Inc.              *
 *                                                                           *
 *   Shows one file to one user, either page by page (with Next, Previous,   *
 *   Top, go-to-page and keyword search) or as a continuous feed.  Both      *
 *   modes are built from the same piece: pg_build() (FVPAGE.C) reads the    *
 *   file from a given offset and produces up to N screen lines of output,   *
 *   handling line endings, word wrap, ANSI escapes and IBM PC characters.   *
 *                                                                           *
 *   Output never goes straight to the user.  It is placed in a queue        *
 *   (out[] in the user's work area) and sent as the channel's output        *
 *   buffer has room - and, if a baud rate is being simulated, no faster     *
 *   than that rate allows.  While anything is left to send, a polling       *
 *   routine keeps the queue moving; nothing here ever waits, because every  *
 *   user on the BBS shares one thread.                                      *
 *                                                                           *
 *   How the content types differ:                                           *
 *     TEXT  word-wrapped to the screen; escape sequences removed.           *
 *     ANSI  not wrapped (the art is drawn for the screen); escape sequences *
 *           kept for ANSI callers and removed for others.                   *
 *     IBM   IBM PC (code page 437) art: not wrapped.                        *
 *   Callers without ANSI see ANSI and IBM line-drawing characters as plain  *
 *   ASCII look-alikes.  A Ctrl-Z ends the file (it marks the start of the   *
 *   SAUCE description block on ANSI art), and a form feed starts a new      *
 *   page.                                                                   *
 *                                                                           *
 *   A document may have a header: another file shown above it, which stays  *
 *   on screen while the document scrolls (see "Headers" below).             *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

/* Milliseconds since Windows started, for the baud-rate simulation.        */
/* (Declared here rather than including windows.h, which clashes with the   */
/* SDK's own type names.)                                                   */
__declspec(dllimport) unsigned long __stdcall GetTickCount(void);

#define V        (&fvv->u.v)         /* this user's viewer state             */
#define CHUNK    512                /* most we hand the channel at once     */
#define MARGIN   96                  /* output room we leave for prompts     */

static GBOOL polling[MAXNTERM];      /* is vwpoll() running for a channel?   */
static SHORT pinrows[MAXNTERM];      /* screen rows while a header is pinned */
                                     /*   (scroll region set), 0 = none.     */
                                     /*   Not in the work area: it must be   */
                                     /*   right even after the user leaves.  */

static VOID showpage(VOID);
static VOID pagebody(VOID);
static VOID feedstart(VOID);
static VOID feednext(VOID);
static VOID hdrnext(VOID);
static VOID pump(VOID);

/* ------------------------------------------------------------------------ *
 * Sending prfbuf.  The engine's outprf() transmits prfbuf and rewinds its  *
 * write pointer but does NOT empty it (WG33 gcommlib PRF.C outprf; the v10 *
 * WGSERVER.EXE outprf has no terminating store, unlike clrprf).  A second  *
 * outprf() with nothing new written in between therefore sends the last    *
 * text again - which showed callers each page prompt and "Searching..."    *
 * twice.  Every send in this module goes through fv_out(), which empties   *
 * the buffer afterwards, so an extra flush is always harmless.             *
 * ------------------------------------------------------------------------ */

VOID
fv_out(VOID)                         /* send prfbuf to the user, then empty  */
{
     outprf(usrnum);
     clrprf();
}

/* ------------------------------------------------------------------------ *
 * The output queue and the baud-rate simulation.                           *
 * ------------------------------------------------------------------------ */

static VOID
rawout(VOID)                         /* while viewing: send the file as is   */
{
     /* The BBS word-wraps output at the caller's screen width and pauses   */
     /* it every screenful.  Both break a document: the wrap tears ANSI art */
     /* that relies on the terminal wrapping at the last column (seen on a  */
     /* board: spaces in a header replaced by line breaks), and the pause   */
     /* lands in the middle of a page.  The engine shows .ANS menu files    */
     /* the same way (WG33 MENUING.C: btutsw 0 and btuxnf 0,19, restored    */
     /* with btutsw scnwid and rstrxf).  The BBS turns the pause back on    */
     /* between input cycles (seen during a search), so this runs whenever  */
     /* the viewer is about to send: on opening, on each input line and on */
     /* each polling call.                                                  */
     btutsw(usrnum, 0);
     btuxnf(usrnum, 0, 19);
}

static VOID
vwpoll(VOID);

static VOID
startpoll(VOID)
{
     if (!polling[usrnum]) {
          polling[usrnum] = TRUE;
          begin_polling(usrnum, vwpoll);
     }
}

static VOID
stoppoll(VOID)
{
     if (polling[usrnum]) {
          polling[usrnum] = FALSE;
          stop_polling(usrnum);
     }
}

static VOID
queue(INT len)                       /* out[0..len) is ready to send         */
{
     V->outlen = len;
     V->outpos = 0;
     V->credit = 0;
     V->lastms = GetTickCount();
}

static GBOOL                         /*   TRUE when the queue is empty       */
qsend(VOID)                          /* send what we can right now           */
{
     CHAR tmp[CHUNK + 1];
     INT room, n;
     ULONG ms, gap;

     fv_out();                       /* anything already in prfbuf first     */
     while (V->outpos < V->outlen) {
          room = btuoba(usrnum) - MARGIN;
          if (room < 64) {
               return FALSE;         /* channel busy: try again next cycle   */
          }
          n = V->outlen - V->outpos;
          if (n > room) {
               n = room;
          }
          if (n > CHUNK) {
               n = CHUNK;
          }
          if (V->baud > 0 && !V->rush) {
               /* A modem sends about one character per 10 bits (8 data,    */
               /* start and stop), so baud/10 characters a second.  Credit  */
               /* is kept in thousandths of a character so that slow rates  */
               /* still add up exactly.                                     */
               ms = GetTickCount();
               gap = ms - V->lastms;
               V->lastms = ms;
               if (gap > 1000) {
                    gap = 1000;      /* idle a while: no huge burst          */
               }
               V->credit += (LONG)gap * V->baud / 10L;
               if (V->credit > V->baud * 25L + 1000L) {
                    V->credit = V->baud * 25L + 1000L;   /* 1/4 second max  */
               }
               if (V->credit < 1000L) {
                    return FALSE;
               }
               if (n > V->credit / 1000L) {
                    n = (INT)(V->credit / 1000L);
               }
               V->credit -= n * 1000L;
          }
          movmem(V->out + V->outpos, tmp, n);
          tmp[n] = '\0';
          prf("%s", tmp);
          fv_out();
          V->outpos += n;
     }
     return TRUE;
}

static VOID
pageprompt(VOID)                     /* the prompt under a page              */
{
     GBOOL hasnext = (V->lastpg != V->cpage);
     GBOOL hasprev = (V->cpage > 0);
     LONG pct;

     pct = (V->lastpg == V->cpage || V->fsize <= 0) ? 100L
         : (LONG)((double)V->pgend * 100.0 / (double)V->fsize);
     if (pct > 100) {
          pct = 100;
     }
     tv_setn(TV_PAGE, V->cpage + 1);
     tv_setn(TV_PCT, pct);
     tv_set(TV_TITLE, V->title);
     if (V->found[0] != '\0') {
          tv_set(TV_WORD, V->found);
          prfmsg(KWFOUND);
          V->found[0] = '\0';
     }
     prfmsg(hasnext ? (hasprev ? PGBOTH : PGNEXT) : (hasprev ? PGPREV : PGONLY));
     usrptr->substt = ST_PAGER;
}

static VOID
qdone(VOID)                          /* the queue has been sent              */
{
     if (V->after == FVAFT_FEED) {
          feednext();
     }
     else if (V->after == FVAFT_HDR) {
          hdrnext();
     }
     else {
          stoppoll();
          pageprompt();
          fv_out();
     }
}

static VOID
pump(VOID)                           /* send now; poll for whatever is left  */
{
     if (qsend()) {
          qdone();
     }
     else {
          startpoll();
     }
}

/* ------------------------------------------------------------------------ *
 * Headers.  A document may name a header, another file shown above it     *
 * (FVDOC.H).  vw_open() measures it and picks one of three ways to show it  *
 * (FVH_xxx):                                                               *
 *   PIN   ANSI callers, room to spare: the header is drawn once and the     *
 *         rows under it become the terminal's scrolling region (DECSTBM),   *
 *         so the document scrolls and pages beneath a header that stays.    *
 *         Each page clears only the region.  The region is reset whenever   *
 *         the viewer closes.                                                *
 *   EACH  callers without ANSI: the header is sent at the top of every      *
 *         page (once, at the start, for a continuous display).              *
 *   ONCE  too tall to keep (fewer than FV_HMINDOC document lines would be   *
 *         left): shown once at the start, then it scrolls away.             *
 * PIN and EACH take the header's height out of each page.  The header goes *
 * through the output queue in chunks like any page, so the simulated baud   *
 * rate applies to it too.                                                   *
 * ------------------------------------------------------------------------ */

static INT                           /*   bytes placed in out[]              */
hbuild(                              /* header output from an offset         */
LONG start,
LONG *end,
GBOOL *eof)
{
     FILE *fp, *doc;
     SHORT type;
     INT n;

     if ((fp = fopen(V->hpath, "rb")) == NULL) {
          *end = start;
          *eof = TRUE;
          pg_lines = 0;
          pg_part = FALSE;
          V->out[0] = '\0';
          return 0;
     }
     doc = fvfp[usrnum];             /* pg_build() reads the channel's file  */
     type = V->type;                 /*   with the viewer's content type     */
     fvfp[usrnum] = fp;
     V->type = V->htype;
     n = pg_build(start, V->out, 0, FALSE, end, eof);
     V->type = type;
     fvfp[usrnum] = doc;
     fclose(fp);
     if (*end <= start) {
          *eof = TRUE;               /* never loop on the same spot          */
     }
     return n;
}

static INT                           /*   lines, 0 = empty or unreadable     */
hmeasure(VOID)                       /* how tall is the header?              */
{
     LONG pos = 0L, end;
     GBOOL eof = FALSE;
     INT i, rows = 0;

     for (i = 0 ; i < FV_HMAXCHK && !eof ; i++) {
          hbuild(pos, &end, &eof);
          rows += pg_lines;
          pos = end;
     }
     if (!eof) {
          return 9999;               /* far too big to keep on screen        */
     }
     return rows + (pg_part ? 1 : 0);
}

static VOID
headsetup(const struct fvhead *hd)   /* vw_open(): how to show the header    */
{
     FILE *fp;
     INT h;

     if ((fp = fopen(hd->path, "rb")) == NULL) {
          return;                    /* gone: show the document without it   */
     }
     fclose(fp);
     stzcpy(V->hpath, hd->path, sizeof(V->hpath));
     V->htype = (hd->type == FVY_AUTO) ? vw_detect(hd->path) : hd->type;
     h = (hd->rows > 0) ? hd->rows : hmeasure();
     if (h <= 0) {
          V->hpath[0] = '\0';        /* an empty header: none                */
          return;
     }
     V->hrows = (SHORT)(h > 9999 ? 9999 : h);
     if (V->ansi && V->rows - 1 - V->hrows - 2 >= FV_HMINDOC) {
          /* The scrolling region stops one row short of the screen's last */
          /* row.  MuffinTerm scrolled the pinned header off the top when  */
          /* the region reached the last row (a tester's 24-row screen),   */
          /* and was right at 23; the last row is simply left unused.      */
          V->hmode = FVH_PIN;
          V->rows--;
          V->lines = (SHORT)(V->rows - V->hrows - 2);
     }
     else if (!V->ansi && V->rows - V->hrows - 2 >= FV_HMINDOC) {
          V->hmode = FVH_EACH;
          V->lines = (SHORT)(V->rows - V->hrows - 2);
     }
     else {
          V->hmode = FVH_ONCE;
     }
}

static VOID
unpin(VOID)                          /* undo a pinned header's scroll region */
{
     if (pinrows[usrnum] != 0) {
          /* Setting the region homes the cursor; put it back at the       */
          /* bottom so what follows scrolls the whole screen as usual.      */
          prf("\33[0m\33[r\33[%d;1H", pinrows[usrnum]);
          pinrows[usrnum] = 0;
     }
}

static VOID
hdrstart(                            /* draw the header, then...             */
SHORT then)                          /*   FVAFT_PROMPT (a page) or FEED      */
{
     V->hthen = then;
     V->hpos = 0L;
     V->hend = FALSE;
     if (V->ansi) {
          unpin();                   /* the whole screen again, then clear   */
          prfmsg(PGCLS);
     }
     else {
          prfmsg(PGSEP);
     }
     usrptr->substt = (then == FVAFT_FEED) ? ST_FEED : ST_BUSY;
     hdrnext();
}

static VOID
hdrdone(VOID)                        /* the header has been sent             */
{
     V->hshown = TRUE;
     if (V->ansi) {
          prf("\33[0m");
     }
     if (V->hmode == FVH_PIN) {
          /* The rows under the header scroll; the header stays.  Setting   */
          /* the region moves the cursor, so place it afterwards.           */
          prf("\33[%d;%dr\33[%d;1H", V->hrows + 1, V->rows, V->hrows + 1);
          pinrows[usrnum] = V->rows;
     }
     else if (V->hpart) {
          prf("\n");                 /* finish its last line                 */
     }
     V->hfresh = TRUE;
     if (V->hthen == FVAFT_FEED) {
          feedstart();
     }
     else {
          pagebody();
     }
}

static VOID
hdrnext(VOID)                        /* queue the next chunk of the header   */
{
     LONG end;
     GBOOL eof;
     INT n;

     if (V->hend) {
          hdrdone();
          return;
     }
     n = hbuild(V->hpos, &end, &eof);
     V->hpart = (SHORT)pg_part;      /* (pg_part is shared: keep it now)     */
     V->hpos = end;
     V->hend = (SHORT)eof;
     queue(n);
     V->after = FVAFT_HDR;
     startpoll();                    /* polling sends it, then calls us back */
}

/* ------------------------------------------------------------------------ *
 * Paged mode.                                                              *
 * ------------------------------------------------------------------------ */

static VOID
showpage(VOID)                       /* show page cpage (header first?)      */
{
     V->rush = FALSE;
     if (V->hmode == FVH_EACH
      || ((V->hmode == FVH_PIN || V->hmode == FVH_ONCE) && !V->hshown)) {
          hdrstart(FVAFT_PROMPT);
          return;
     }
     pagebody();
}

static VOID
pagebody(VOID)                       /* build page cpage and start sending   */
{
     LONG end;
     GBOOL eof;
     INT n, r;

     n = pg_build(V->ppos[V->cpage], V->out, V->lines, TRUE, &end, &eof);
     V->pgend = end;
     if (V->cpage + 1 < FV_MAXPAGE) {
          V->ppos[V->cpage + 1] = end;
          if (V->known < V->cpage + 1) {
               V->known = V->cpage + 1;
          }
     }
     if (eof) {
          V->lastpg = V->cpage;
     }
     if (V->hfresh) {                /* straight under the header just drawn */
          V->hfresh = FALSE;
     }
     else if (pinrows[usrnum] != 0) {     /* clear only under the header    */
          /* Row by row, bottom up, ending on the first row under the       */
          /* header.  Not ESC[J: SyncTERM's erase-to-end-of-screen counts   */
          /* rows from the scroll region's top margin, so with a header     */
          /* pinned it misses rows and the old page shows through.          */
          prf("\33[0m");
          for (r = pinrows[usrnum] ; r > V->hrows ; r--) {
               prf("\33[%d;1H\33[K", r);
          }
     }
     else {
          prfmsg(V->ansi ? PGCLS : PGSEP);  /* clear screen (or a blank line)*/
     }
     queue(n);
     V->after = FVAFT_PROMPT;
     usrptr->substt = ST_BUSY;
     pump();
}

/* ------------------------------------------------------------------------ *
 * Continuous mode.                                                         *
 * ------------------------------------------------------------------------ */

static VOID
feednext(VOID)                       /* queue the next chunk, or finish      */
{
     LONG end;
     GBOOL eof;
     INT n;

     if (V->feedeof) {
          stoppoll();
          usrptr->substt = ST_DONE;
          if (V->opts & FVO_RETURN) {
               /* Straight back, no "Press Enter".  We are in the polling   */
               /* routine, which cannot leave the module itself, so hand    */
               /* the engine an empty input line: it calls our input        */
               /* routine (ST_DONE leaves), and a FALSE return there takes  */
               /* the user to the menu (WG33 MAJORBBS.C, CRSTG -> hdlcri;   */
               /* the engine's own file transfer does the same, FILEXFER.C  */
               /* fupfin).                                                  */
               btuinj(usrnum, CRSTG);
               return;
          }
          tv_set(TV_TITLE, V->title);
          prfmsg(FEEDEND);
          fv_out();
          return;
     }
     n = pg_build(V->feedpos, V->out, FV_FEEDLN, FALSE, &end, &eof);
     if (end <= V->feedpos) {
          eof = TRUE;                /* never loop on the same spot          */
     }
     V->feedpos = end;
     V->feedeof = (SHORT)eof;
     queue(n);
     V->after = FVAFT_FEED;
     usrptr->substt = ST_FEED;
     startpoll();                    /* the feed always runs from polling    */
}

static VOID
feedstart(VOID)                      /* the feed itself, after any header    */
{
     V->hfresh = FALSE;
     if (!(V->opts & FVO_QUIET)) {
          prfmsg(FEEDBGN);           /* "(Continuous display - ...)"        */
     }
     feednext();
}

static VOID
startmode(SHORT mode, LONG from)     /* begin paged or continuous display    */
{
     fvv->mode = mode;
     if (mode == FVM_FEED) {
          V->feedpos = from;
          V->feedeof = FALSE;
          if (V->hmode != FVH_NONE && !V->hshown) {
               hdrstart(FVAFT_FEED);
          }
          else {
               feedstart();
          }
     }
     else {
          showpage();
     }
}

/* ------------------------------------------------------------------------ *
 * Keyword search.                                                          *
 * ------------------------------------------------------------------------ */

static VOID
searchstep(VOID)                     /* search a few pages (from polling)    */
{
     LONG end;
     GBOOL eof;
     INT i, k, p;

     for (i = 0 ; i < FV_SRCHPC ; i++) {
          p = V->kwpage;
          if (p < 0 || p >= FV_MAXPAGE || p > V->known
           || (V->lastpg >= 0 && p > V->lastpg)) {
               V->searching = FALSE;            /* ran off the end: none     */
               stoppoll();
               prfmsg(KWNONE);
               pageprompt();
               fv_out();
               return;
          }
          pg_build(V->ppos[p], V->out, V->lines, TRUE, &end, &eof);
          if (p + 1 < FV_MAXPAGE && V->known < p + 1) {
               V->ppos[p + 1] = end;
               V->known = p + 1;
          }
          if (eof) {
               V->lastpg = p;
          }
          if ((k = pg_match(V->out)) >= 0) {  /* found: show that page     */
               V->searching = FALSE;
               stoppoll();
               stzcpy(V->found, V->kwds[k], FV_KWSIZ);
               V->cpage = p;
               showpage();
               return;
          }
          V->kwpage += (V->kwdir == 'B') ? -1 : 1;
     }
}

static VOID
searchgo(VOID)                       /* start (or repeat) a search           */
{
     V->kwact = TRUE;
     V->kwpage = V->cpage + ((V->kwdir == 'B') ? -1 : 1);
     V->searching = TRUE;
     usrptr->substt = ST_BUSY;
     prfmsg(KWWAIT);
     fv_out();
     startpoll();
}

static VOID
setkwds(INT first)                   /* keywords from margv[first..]         */
{
     INT i;
     CHAR *s, *d;

     V->nkw = 0;
     for (i = first ; i < margc && V->nkw < FV_NKW ; i++) {
          d = V->kwds[V->nkw];
          for (s = margv[i] ; *s != '\0' && d - V->kwds[V->nkw] < FV_KWSIZ - 1 ;
               s++) {
               if (isalnum((UCHAR)*s)) {
                    *d++ = *s;
               }
          }
          *d = '\0';
          if (V->kwds[V->nkw][0] != '\0') {
               V->nkw++;
          }
     }
}

/* ------------------------------------------------------------------------ *
 * Polling: keeps the queue moving and runs searches.                        *
 * ------------------------------------------------------------------------ */

static VOID
vwpoll(VOID)
{
     setmbk(fvmb);
     if (usrptr->state != fvstt) {   /* the engine took the user elsewhere   */
          unpin();                   /*   (e.g. a sysop sent them to a menu) */
          fv_out();
          vw_hangup(usrnum);
     }
     else if (fvfp[usrnum] == NULL) {     /* nothing to do any more          */
          stoppoll();
     }
     else {
          rawout();                  /* (a search sends its own messages)    */
          if (V->searching) {
               if (btuoba(usrnum) > MARGIN) {
                    searchstep();
               }
          }
          else if (qsend()) {
               qdone();
          }
     }
     rstmbk();
}

/* ------------------------------------------------------------------------ *
 * Opening, input and closing.                                              *
 * ------------------------------------------------------------------------ */

GBOOL                                /*   FALSE if the file would not open   */
vw_open(                             /* start viewing a file                 */
const CHAR *path,
const CHAR *title,                   /*   "" = use the file name             */
SHORT type,                          /*   FVY_xxx (AUTO is resolved here)    */
SHORT mode,                          /*   FVM_PAGED, FVM_FEED or FVM_ASK     */
LONG baud,                           /*   simulated rate, 0 = full speed     */
SHORT back,                          /*   FVBACK_xxx: where X goes           */
SHORT opts,                          /*   FVO_QUIET, FVO_RETURN              */
const struct fvhead *hd)             /*   header to show above it, or NULL   */
{
     FILE *fp;
     INT w, rows;

     vw_close();
     if ((fp = fopen(path, "rb")) == NULL) {
          tv_set(TV_FILE, path);
          prfmsg(NOOPEN);
          return FALSE;
     }
     fvfp[usrnum] = fp;
     setmem(V, sizeof(*V), 0);
     stzcpy(V->path, path, sizeof(V->path));
     if (*title != '\0') {
          stzcpy(V->title, title, sizeof(V->title));
     }
     else {
          fileparts(GCPART_FNAM, path, V->title, sizeof(V->title));
     }
     fseek(fp, 0L, SEEK_END);
     V->fsize = ftell(fp);
     fseek(fp, 0L, SEEK_SET);
     V->type = (type == FVY_AUTO) ? vw_detect(path) : type;
     V->ansi = (usaptr->ansifl & ANSON) != 0;

     /* Screen geometry.  Text wraps two columns short of the screen width */
     /* (as the original did), because the BBS itself wraps output that     */
     /* reaches the last column.  The page leaves room for the prompt.      */
     w = usaptr->scnwid;
     V->termw = (SHORT)((w < 40 || w > 255) ? 80 : w);
     V->wrapw = (SHORT)((V->termw > 80 ? 80 : V->termw) - 2);
     rows = usaptr->scnbrk;
     if (rows < 6 || rows > 200) {
          rows = 24;
     }
     V->rows = (SHORT)rows;
     V->lines = (SHORT)(rows - 2);
     V->lastpg = -1;
     V->baud = baud;
     V->opts = opts;
     fvv->back = back;
     if (hd != NULL && hd->path[0] != '\0') {
          headsetup(hd);             /* may take its height out of a page    */
     }

     fv_guard(TRUE);                 /* no pages or global commands mid-file */
     rawout();                       /* no BBS word wrap or page pauses      */
     if (mode == FVM_ASK) {
          tv_set(TV_TITLE, V->title);
          prfmsg(ASKMODE);
          usrptr->substt = ST_ASKMODE;
          return TRUE;
     }
     startmode(mode, 0L);
     return TRUE;
}

static GBOOL
leave(VOID)                          /* done with this document              */
{
     vw_close();
     return lib_back();
}

static VOID
baudcmd(VOID)                        /* B [rate]: show or change the rate    */
{
     LONG n;

     if (margc >= 2) {
          if ((n = fv_parsebaud(margv[1], FALSE)) < 0) {
               prfmsg(BAUDBAD);
          }
          else {
               V->baud = n;
               tv_set(TV_BAUD, n == 0 ? "FULL SPEED" : fv_baudname(n));
               prfmsg(BAUDSET);
          }
     }
     else {
          tv_set(TV_BAUD, V->baud == 0 ? "FULL SPEED" : fv_baudname(V->baud));
          prfmsg(BAUDNOW);
     }
}

static GBOOL
pager(VOID)                          /* input at the page prompt             */
{
     CHAR cmd;
     INT pg;

     if (margc == 0) {               /* Enter: search again, or next page    */
          if (V->kwact) {
               searchgo();
               return TRUE;
          }
          cmd = 'N';
     }
     else if (isdigit((UCHAR)margv[0][0])) {
          pg = atoi(margv[0]) - 1;   /* a page number                        */
          if (pg >= 0 && pg <= V->known && pg < FV_MAXPAGE
           && (V->lastpg < 0 || pg <= V->lastpg)) {
               V->cpage = pg;
               V->kwact = FALSE;
               showpage();
          }
          else {
               tv_setn(TV_PAGE, V->known + 1);
               prfmsg(PGFAR);
               pageprompt();
          }
          return TRUE;
     }
     else {
          cmd = (CHAR)toupper((UCHAR)margv[0][0]);
     }
     switch (cmd) {
     case 'N':
          V->kwact = FALSE;
          if (V->lastpg == V->cpage) {
               return leave();       /* past the end: done, as before        */
          }
          if (V->cpage + 1 >= FV_MAXPAGE) {
               prfmsg(FVWBIG);
               pageprompt();
               return TRUE;
          }
          V->cpage++;
          showpage();
          return TRUE;
     case 'P':
          V->kwact = FALSE;
          if (V->cpage > 0) {
               V->cpage--;
          }
          showpage();
          return TRUE;
     case 'T':
          V->kwact = FALSE;
          V->cpage = 0;
          showpage();
          return TRUE;
     case 'C':                       /* continuous after the page on screen  */
          V->kwact = FALSE;
          startmode(FVM_FEED, V->pgend);
          return TRUE;
     case 'K':
          if (margc >= 2) {          /* K word word ... (keywords inline)    */
               setkwds(1);
               if (V->nkw > 0) {
                    prfmsg(KWDIR);
                    usrptr->substt = ST_KWDIR;
                    return TRUE;
               }
          }
          prfmsg(KWASK);
          usrptr->substt = ST_KWDS;
          return TRUE;
     case 'B':
          baudcmd();
          pageprompt();
          return TRUE;
     case 'X':
     case 'Q':
          return leave();
     default:
          prfmsg(PGHELP);
          pageprompt();
          return TRUE;
     }
}

GBOOL
vw_input(VOID)                       /* input while viewing a document       */
{
     if (fvfp[usrnum] != NULL) {
          rawout();                  /* (prompts and messages too)           */
     }
     switch (usrptr->substt) {
     case ST_ASKMODE:
          if (margc > 0 && toupper((UCHAR)margv[0][0]) == 'X') {
               return leave();
          }
          startmode((margc > 0 && toupper((UCHAR)margv[0][0]) == 'C')
                    ? FVM_FEED : FVM_PAGED, 0L);
          return TRUE;
     case ST_PAGER:
          return pager();
     case ST_KWDS:
          setkwds(0);
          if (V->nkw == 0) {
               prfmsg(KABT);
               pageprompt();
               return TRUE;
          }
          prfmsg(KWDIR);
          usrptr->substt = ST_KWDIR;
          return TRUE;
     case ST_KWDIR:
          if (margc > 0 && toupper((UCHAR)margv[0][0]) == 'X') {
               prfmsg(KABT);
               pageprompt();
               return TRUE;
          }
          V->kwdir = (margc > 0 && toupper((UCHAR)margv[0][0]) == 'B')
                   ? 'B' : 'F';
          searchgo();
          return TRUE;
     case ST_BUSY:
          if (V->searching) {        /* any input stops a search             */
               V->searching = FALSE;
               V->kwact = FALSE;
               stoppoll();
               prfmsg(KABT);
               pageprompt();
               return TRUE;
          }
          if (margc > 0 && toupper((UCHAR)margv[0][0]) == 'X') {
               return leave();
          }
          V->rush = TRUE;            /* skip the baud delay for this page    */
          pump();
          return TRUE;
     case ST_FEED:                   /* any input stops the feed             */
          stoppoll();
          V->outlen = V->outpos = 0;
          if (!(V->opts & FVO_QUIET)) {
               prfmsg(FEEDSTOP);
          }
          return leave();
     case ST_DONE:
     default:
          return leave();
     }
}

VOID
vw_close(VOID)                       /* stop viewing; safe to call twice     */
{
     stoppoll();
     unpin();                        /* a pinned header's region goes too    */
     if (fvfp[usrnum] != NULL) {
          fclose(fvfp[usrnum]);
          fvfp[usrnum] = NULL;
          btutsw(usrnum, usaptr->scnwid);  /* the BBS word wrap again, and  */
          rstrxf();                  /*   the user's own page pauses         */
          fv_guard(FALSE);
     }
}

VOID
vw_hangup(INT unum)                  /* the channel dropped: tidy up         */
{
     pinrows[unum] = 0;              /* (also a new caller on the channel)   */
     if (polling[unum]) {
          polling[unum] = FALSE;
          stop_polling(unum);
     }
     if (fvfp[unum] != NULL) {
          fclose(fvfp[unum]);
          fvfp[unum] = NULL;
     }
}

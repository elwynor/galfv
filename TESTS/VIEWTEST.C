/* Build 26.10.27.1 08:06PM */
/*****************************************************************************
 *   VIEWTEST.C                               File Viewer - Viewer session    *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   Runs the real viewer (SRC/GALFV/FVVIEW.C and FVPAGE.C) against a small  *
 *   stand-in for the BBS and records every byte "sent" to the caller.  The  *
 *   stand-in's prfbuf behaves like the engine's: outprf() sends prfbuf and  *
 *   rewinds the write pointer but does not empty the buffer, and clrprf()  *
 *   empties it (checked against the v10 WGSERVER.EXE).                      *
 *                                                                           *
 *   The checks assert one invariant: every prompt reaches the caller        *
 *   exactly once per time it is shown.  Each user input is processed as     *
 *   GALFV.C's fvinput() does: the routine, then a final send.               *
 *                                                                           *
 *   Build and run with TOOLS/RUNTESTS.PY.  Exit code 0 = every check        *
 *   passed.                                                                 *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

#undef printf                        /* the SDK maps it to its own gprintf  */

/* ---- Stand-ins for the engine ------------------------------------------- */

INT usrnum = 0;
CHAR *vdaptr;
FILE *fvfp[MAXNTERM];
struct user *usrptr;
struct usracc *usaptr;
INT margc;
CHAR *margv[INPSIZ / 2];
CHAR input[INPSIZ];
HMCVFILE fvmb;
INT fvstt = 7;
struct fvsetdat fvcfg;

static struct user theuser;
static struct usracc theacc;
static CHAR pbuf[8192];              /* prfbuf and its write pointer        */
static CHAR *pptr = pbuf;
static CHAR wire[200000];            /* everything sent to the caller       */
static VOID (*pollrou)(VOID);        /* the polling routine, if any         */

/* The channel's word wrap (btutsw) and page pauses (btuxnf 0,19 off,     */
/* rstrxf on).  The BBS turns them back on between cycles (seen on a      */
/* board), so rearm() does that before each input line and polling call. */
static INT tswidth = 80;
static GBOOL pausing = TRUE;
static INT cooked;                   /* viewer sends made with either one on */

VOID prf(const char *fmt, ...)
{
     va_list ap;

     va_start(ap, fmt);
     vsprintf(pptr, fmt, ap);
     va_end(ap);
     pptr += strlen(pptr);
}

VOID prfmsg(INT msgnum, ...)         /* a message shows as <M:n>             */
{
     sprintf(pptr, "<M:%d>", msgnum);
     pptr += strlen(pptr);
}

VOID outprf(INT unum)                /* like the engine: no emptying         */
{
     size_t have = strlen(wire), add = strlen(pbuf);

     (VOID)unum;
     if (add > 0 && fvfp[0] != NULL && (tswidth != 0 || pausing)) {
          cooked++;                  /* viewer output the BBS would alter    */
     }
     if (have + add < sizeof(wire)) {          /* (a runaway resend fills   */
          memcpy(wire + have, pbuf, add + 1);  /*  the wire; stop adding)   */
     }
     pptr = pbuf;
}

VOID clrprf(VOID)
{
     pptr = pbuf;
     pbuf[0] = '\0';
}

static INT injected;                 /* CRSTG events injected               */

INT btuinj(INT chan, INT sts)    { (VOID)chan; injected += (sts == CRSTG); return 1; }
INT btuoba(INT chan)             { (VOID)chan; return 16000; }
INT btutsw(INT chan, INT width)  { (VOID)chan; tswidth = width; return 0; }
INT btuxnf(INT chan, INT xon, INT xoff, ...)
{
     (VOID)chan; (VOID)xon;
     if (xoff == 19) {
          pausing = FALSE;
     }
     return 0;
}
VOID rstrxf(VOID)                { pausing = TRUE; }

static VOID
rearm(VOID)
{
     tswidth = 80;
     pausing = TRUE;
}
VOID begin_polling(INT unum, VOID (*rou)(VOID)) { (VOID)unum; pollrou = rou; }
VOID stop_polling(INT unum)      { (VOID)unum; pollrou = NULL; }
VOID setmbk(HMCVFILE mb)         { (VOID)mb; }
VOID rstmbk(VOID)                { }
VOID tv_set(INT which, const CHAR *v)  { (VOID)which; (VOID)v; }
VOID tv_setn(INT which, LONG v)  { (VOID)which; (VOID)v; }
VOID fv_guard(GBOOL on)          { (VOID)on; }
GBOOL lib_back(VOID)             { return FALSE; }
const CHAR *fv_baudname(LONG b)  { (VOID)b; return "x"; }
LONG fv_parsebaud(const CHAR *s, GBOOL d) { (VOID)d; return atol(s); }

GBOOL
sameas(const CHAR *a, const CHAR *b)
{
     return _stricmp(a, b) == 0;
}

CHAR *
stzcpy(CHAR *dst, const CHAR *src, INT n)
{
     strncpy(dst, src, n - 1);
     dst[n - 1] = '\0';
     return dst;
}

CHAR *
fileparts(SHORT part, const CHAR *full, CHAR *dst, SHORT size)
{
     const CHAR *dot = strrchr(full, '.');

     dst[0] = '\0';
     if (part == GCPART_EXTN && dot != NULL) {
          strncpy(dst, dot + 1, size - 1);
          dst[size - 1] = '\0';
     }
     else if (part == GCPART_FNAM) {
          strncpy(dst, full, size - 1);
          dst[size - 1] = '\0';
     }
     return dst;
}

/* ---- Test helpers -------------------------------------------------------- */

static struct fvvda vda;
static INT fails = 0, checks = 0;

#define CHECK(cond, what)                                                  \
     do {                                                                  \
          checks++;                                                        \
          if (!(cond)) {                                                   \
               fails++;                                                    \
               printf("FAIL line %d: %s\n", __LINE__, what);               \
          }                                                                \
     } while (0)

static INT
sent(INT msgnum)                     /* times a message reached the caller   */
{
     CHAR tag[16];
     const CHAR *p = wire;
     INT n = 0;

     sprintf(tag, "<M:%d>", msgnum);
     while ((p = strstr(p, tag)) != NULL) {
          n++;
          p += strlen(tag);
     }
     return n;
}

static VOID
drain(VOID)                          /* let polling run until it stops       */
{
     INT i;

     for (i = 0 ; i < 1000 && pollrou != NULL ; i++) {
          rearm();
          (*pollrou)();
     }
}

static GBOOL
type(const CHAR *line)               /* one line of input, as fvinput() does */
{
     static CHAR buf[INPSIZ];
     CHAR *p;
     GBOOL more;

     strcpy(buf, line);
     margc = 0;
     for (p = strtok(buf, " ") ; p != NULL ; p = strtok(NULL, " ")) {
          margv[margc++] = p;
     }
     rearm();
     more = vw_input();
     fv_out();                       /* fvinput()'s final send               */
     drain();
     return more;
}

static VOID
fresh(VOID)                          /* new session                         */
{
     memset(&vda, 0, sizeof(vda));
     memset(&theuser, 0, sizeof(theuser));
     memset(&theacc, 0, sizeof(theacc));
     vdaptr = (CHAR *)&vda;
     usrptr = &theuser;
     usaptr = &theacc;
     usrptr->state = fvstt;
     usaptr->ansifl = ANSON;
     usaptr->scnwid = 80;
     usaptr->scnbrk = 24;
     wire[0] = '\0';
     clrprf();
     pollrou = NULL;
     injected = 0;
     rearm();
     cooked = 0;
}

static VOID
mkfile(const CHAR *name, INT lines)
{
     FILE *fp = fopen(name, "wb");
     INT i;

     for (i = 1 ; i <= lines ; i++) {
          fprintf(fp, "line %d of the dragon test file\r\n", i);
     }
     fclose(fp);
}

/* ---- The tests ----------------------------------------------------------- */

static VOID
t_one_page_prompt_once(VOID)
{
     fresh();
     mkfile("T_ONE.TXT", 5);
     vw_open("T_ONE.TXT", "One", FVY_TEXT, FVM_PAGED, 0L, FVBACK_EXIT, 0, NULL);
     fv_out();                       /* fvinput()'s final send               */
     drain();
     CHECK(sent(PGONLY) == 1, "a one-page document's prompt is sent once");
     vw_close();
     remove("T_ONE.TXT");
}

static VOID
t_next_page_prompt_once(VOID)
{
     fresh();
     mkfile("T_TWO.TXT", 60);
     vw_open("T_TWO.TXT", "Two", FVY_TEXT, FVM_PAGED, 0L, FVBACK_EXIT, 0, NULL);
     fv_out();
     drain();
     CHECK(sent(PGNEXT) == 1, "page 1's prompt is sent once");
     type("N");
     CHECK(sent(PGBOTH) == 1, "page 2's prompt is sent once");
     type("P");
     CHECK(sent(PGNEXT) == 2, "back on page 1, its prompt once more");
     vw_close();
     remove("T_TWO.TXT");
}

static VOID
t_search_message_once(VOID)
{
     fresh();
     mkfile("T_SRCH.TXT", 200);
     vw_open("T_SRCH.TXT", "Search", FVY_TEXT, FVM_PAGED, 0L, FVBACK_EXIT, 0, NULL);
     fv_out();
     drain();
     type("K dragon");
     CHECK(sent(KWDIR) == 1, "the direction question is sent once");
     type("F");
     CHECK(sent(KWWAIT) == 1, "\"Searching...\" is sent once");
     CHECK(sent(KWFOUND) == 1, "the match notice is sent once");
     vw_close();
     remove("T_SRCH.TXT");
}

static VOID
t_slow_page_prompt_once(VOID)
{
     /* At a simulated baud rate the page finishes from polling, not from    */
     /* the input routine: the prompt must still arrive exactly once.        */
     fresh();
     mkfile("T_SLOW.TXT", 60);
     vw_open("T_SLOW.TXT", "Slow", FVY_TEXT, FVM_PAGED, 115200L, FVBACK_EXIT, 0, NULL);
     fv_out();
     while (pollrou != NULL) {
          (*pollrou)();
     }
     CHECK(sent(PGNEXT) == 1, "a slowly drawn page's prompt is sent once");
     vw_close();
     remove("T_SLOW.TXT");
}

static VOID
t_search_finds_right_page(VOID)
{
     FILE *fp;
     INT i;

     /* 22 text lines per page (screen 24).  "needle" is on lines 50 and   */
     /* 90 only, so it is on page 3 (lines 45-66) and page 5 (89-110):      */
     /* indexes 2 and 4.                                                    */
     fresh();
     fp = fopen("T_NEED.TXT", "wb");
     for (i = 1 ; i <= 130 ; i++) {
          fprintf(fp, (i == 50 || i == 90) ? "line %d has the Needle.\r\n"
                                           : "line %d is hay\r\n", i);
     }
     fclose(fp);
     vw_open("T_NEED.TXT", "Needle", FVY_TEXT, FVM_PAGED, 0L, FVBACK_EXIT, 0, NULL);
     fv_out();
     drain();
     type("K needle");
     type("F");
     CHECK(vda.u.v.cpage == 2, "forward search lands on page 3");
     CHECK(strstr(wire, "line 50 has the Needle") != NULL,
           "and that page is shown");
     type("");                       /* Enter: the next match               */
     CHECK(vda.u.v.cpage == 4, "Enter finds the next match, page 5");
     type("");
     CHECK(sent(KWNONE) == 1, "no more matches is reported");
     CHECK(vda.u.v.cpage == 4, "and the caller stays on the last match");
     type("K needle");
     type("B");
     CHECK(vda.u.v.cpage == 2, "backward search goes back to page 3");
     type("K");
     type("");                       /* no keywords: cancelled              */
     CHECK(sent(KABT) == 1, "an empty keyword line cancels the search");
     CHECK(vda.u.v.cpage == 2, "and leaves the page alone");
     type("K haystack");
     type("F");
     CHECK(sent(KWNONE) == 2, "a word that is not there is not found");
     CHECK(vda.u.v.cpage == 2, "and the page is unchanged");
     vw_close();
     remove("T_NEED.TXT");
}

static VOID
feed(SHORT opts, LONG baud)          /* open T_FEED.TXT continuously        */
{
     fresh();
     mkfile("T_FEED.TXT", 100);
     vw_open("T_FEED.TXT", "Feed", FVY_TEXT, FVM_FEED, baud, FVBACK_EXIT,
             opts, NULL);
     fv_out();                       /* fvinput()'s final send               */
}

static VOID
t_feed_normal(VOID)
{
     feed(0, 0L);
     drain();
     CHECK(sent(FEEDBGN) == 1, "normal: the continuous header is shown");
     CHECK(sent(FEEDEND) == 1, "normal: the end prompt is shown");
     CHECK(injected == 0, "normal: nothing is injected; we wait for Enter");
     CHECK(usrptr->substt == ST_DONE, "normal: waiting at the end prompt");
     vw_close();

     feed(0, 300L);                  /* slow, so it is still running        */
     (*pollrou)();
     type("");                       /* Enter stops it                       */
     CHECK(sent(FEEDSTOP) == 1, "normal: \"Stopped.\" is shown");
     vw_close();
     remove("T_FEED.TXT");
}

static VOID
t_feed_quiet(VOID)
{
     feed(FVO_QUIET, 0L);
     drain();
     CHECK(sent(FEEDBGN) == 0, "quiet: no continuous header");
     CHECK(strstr(wire, "line 100 of") != NULL, "quiet: the file is all sent");
     vw_close();

     feed(FVO_QUIET, 300L);
     (*pollrou)();
     type("");
     CHECK(sent(FEEDSTOP) == 0, "quiet: no \"Stopped.\" line");
     vw_close();
     remove("T_FEED.TXT");
}

static VOID
t_feed_return(VOID)
{
     GBOOL more;

     feed(FVO_RETURN, 0L);
     drain();
     CHECK(sent(FEEDEND) == 0, "return: no end prompt");
     CHECK(injected == 1, "return: one empty input line is injected");
     CHECK(usrptr->substt == ST_DONE, "return: ready to leave on that line");
     more = type("");                /* the engine delivers the injected line*/
     CHECK(!more, "return: that line takes the caller back");
     CHECK(sent(FEEDEND) == 0 && sent(FEEDSTOP) == 0,
           "return: nothing shown on the way out");
     vw_close();
     remove("T_FEED.TXT");
}

static INT
count(const CHAR *s)                 /* times a text reached the caller      */
{
     const CHAR *p = wire;
     INT n = 0;

     while ((p = strstr(p, s)) != NULL) {
          n++;
          p += strlen(s);
     }
     return n;
}

static VOID
t_continuous_carries_on(VOID)
{
     /* C at a page prompt continues after the page on screen: nothing   */
     /* already shown is sent again, and nothing is skipped.               */
     fresh();
     mkfile("T_CONT.TXT", 100);
     vw_open("T_CONT.TXT", "Cont", FVY_TEXT, FVM_PAGED, 0L, FVBACK_EXIT, 0, NULL);
     fv_out();
     drain();
     type("N");                      /* page 2: lines 23-44                  */
     type("C");
     CHECK(count("line 1 of") == 1, "C: page 1 is not sent again");
     CHECK(count("line 23 of") == 1, "C: the page on screen is not resent");
     CHECK(count("line 44 of") == 1, "C: its last line is not resent");
     CHECK(count("line 45 of") == 1, "C: the feed starts on the next line");
     CHECK(count("line 100 of") == 1, "C: and runs to the end");
     vw_close();
     remove("T_CONT.TXT");
}

/* ---- Headers ------------------------------------------------------------- */

static struct fvhead hdr;

static const struct fvhead *
mkhead(INT lines, SHORT rows)        /* T_HEAD.TXT: "HEAD n" lines           */
{
     FILE *fp = fopen("T_HEAD.TXT", "wb");
     INT i;

     for (i = 1 ; i <= lines ; i++) {
          fprintf(fp, "HEAD %d\r\n", i);
     }
     fclose(fp);
     memset(&hdr, 0, sizeof(hdr));
     strcpy(hdr.path, "T_HEAD.TXT");
     hdr.type = FVY_TEXT;
     hdr.rows = rows;
     return &hdr;
}

static VOID
openhd(SHORT mode, INT hlines, SHORT rows, GBOOL ansi)
{
     const struct fvhead *hd = mkhead(hlines, rows);

     fresh();
     usaptr->ansifl = ansi ? ANSON : 0;
     mkfile("T_BODY.TXT", 100);
     vw_open("T_BODY.TXT", "Body", FVY_TEXT, mode, 0L, FVBACK_EXIT, 0, hd);
     fv_out();
     drain();
}

static VOID
t_header_pinned(VOID)
{
     /* ANSI, screen 24, header 3 lines: drawn once, rows 4-23 scroll     */
     /* (never the last row: MuffinTerm scrolled the header away when the */
     /* region reached it), and each page holds 23 - 3 - 2 = 18 lines.     */
     openhd(FVM_PAGED, 3, 0, TRUE);
     CHECK(count("HEAD 3") == 1, "pinned: the header is drawn");
     CHECK(count("\33[4;23r") == 1, "pinned: rows 4-23 become the region");
     CHECK(count(";24r") == 0, "pinned: the region never reaches row 24");
     CHECK(count("line 18 of") == 1 && count("line 19 of") == 0,
           "pinned: page 1 is 18 lines, under the header");
     type("N");
     type("N");
     type("P");
     CHECK(count("HEAD 1") == 1, "pinned: paging never resends it");
     CHECK(count("\33[4;1H\33[K") == 3 && count("\33[23;1H\33[K") == 3,
           "pinned: each page erases every row of the region");
     CHECK(count("\33[24;1H\33[K") == 0, "pinned: row 24 is never used");
     CHECK(count("\33[3;1H\33[K") == 0, "pinned: the header rows are left");
     CHECK(count("\33[J") == 0, "pinned: never ESC[J (SyncTERM misplaces it)");
     CHECK(count("line 19 of") == 2, "pinned: page 2 starts on line 19");
     CHECK(sent(PGCLS) == 1, "pinned: the screen is cleared only once");
     type("C");
     CHECK(count("HEAD 1") == 1, "pinned: C keeps it, no resend");
     CHECK(count("line 100 of") == 1, "pinned: C runs to the end");
     CHECK(count("\33[r") == 0, "pinned: the region is still set");
     vw_close();                     /* (on the board fvinput() then sends)  */
     fv_out();
     CHECK(count("\33[r") == 1, "pinned: closing resets the region");
     vw_close();
     fv_out();
     CHECK(count("\33[r") == 1, "pinned: and only once");
     remove("T_BODY.TXT");
     remove("T_HEAD.TXT");
}

static VOID
t_header_pinned_feed(VOID)
{
     openhd(FVM_FEED, 3, 0, TRUE);
     CHECK(count("HEAD 1") == 1, "pinned feed: the header is drawn once");
     CHECK(count("\33[4;23r") == 1, "pinned feed: the region is set");
     CHECK(strstr(wire, "HEAD 3") < strstr(wire, "\33[4;23r")
        && strstr(wire, "\33[4;23r") < strstr(wire, "line 1 of"),
           "pinned feed: header, then region, then the document");
     CHECK(count("line 100 of") == 1, "pinned feed: the whole document");
     vw_close();
     fv_out();
     CHECK(count("\33[r") == 1, "pinned feed: the region is reset");
     remove("T_BODY.TXT");
     remove("T_HEAD.TXT");
}

static VOID
t_header_each_page(VOID)
{
     /* No ANSI: no region; the header tops every page instead.           */
     openhd(FVM_PAGED, 3, 0, FALSE);
     CHECK(count("HEAD 3") == 1, "no ANSI: the header tops page 1");
     CHECK(count("line 19 of") == 1 && count("line 20 of") == 0,
           "no ANSI: page 1 is 19 lines");
     type("N");
     CHECK(count("HEAD 3") == 2, "no ANSI: and page 2");
     CHECK(count("\33[") == 0, "no ANSI: no escape sequences at all");
     vw_close();
     remove("T_BODY.TXT");
     remove("T_HEAD.TXT");
}

static VOID
t_header_too_tall(VOID)
{
     /* 18 lines leave 24 - 18 - 2 = 4 document lines: shown once only.   */
     openhd(FVM_PAGED, 18, 0, TRUE);
     CHECK(count("HEAD 18") == 1, "tall: the header is shown");
     CHECK(count("\33[r") == 0 && count(";24r") == 0, "tall: no region");
     CHECK(count("line 22 of") == 1 && count("line 23 of") == 0,
           "tall: pages are full height");
     type("N");
     CHECK(count("HEAD 1\n") == 1, "tall: not shown again");
     vw_close();
     remove("T_BODY.TXT");
     remove("T_HEAD.TXT");
}

static VOID
t_header_height_by_hand(VOID)
{
     /* A height set by hand wins over the count (ANSI art that moves     */
     /* the cursor cannot be counted).                                     */
     openhd(FVM_PAGED, 3, 6, TRUE);
     CHECK(count("\33[7;23r") == 1, "by hand: the region starts at row 7");
     CHECK(count("line 15 of") == 1 && count("line 16 of") == 0,
           "by hand: pages are 23 - 6 - 2 = 15 lines");
     vw_close();
     remove("T_BODY.TXT");
     remove("T_HEAD.TXT");
}

static VOID
t_header_missing(VOID)
{
     const struct fvhead *hd = mkhead(3, 0);

     remove("T_HEAD.TXT");           /* named, but the file is gone          */
     fresh();
     mkfile("T_BODY.TXT", 100);
     vw_open("T_BODY.TXT", "Body", FVY_TEXT, FVM_PAGED, 0L, FVBACK_EXIT, 0, hd);
     fv_out();
     drain();
     CHECK(count("line 22 of") == 1 && count(";24r") == 0,
           "missing header: the document as if it had none");
     vw_close();
     remove("T_BODY.TXT");
}

static VOID
t_raw_output(VOID)
{
     /* Everything the viewer sends goes out with the BBS's word wrap and  */
     /* page pauses off, even though the BBS turns them back on between    */
     /* cycles; leaving restores both.                                      */
     openhd(FVM_PAGED, 3, 0, TRUE);
     usaptr->scnwid = 132;           /* (restored to the user's own width)  */
     CHECK(cooked == 0, "raw: the header and page 1");
     type("N");
     type("K haystack");
     type("F");                      /* no match: sent from polling          */
     type("B 300");
     type("T");                      /* a slow page, sent from polling       */
     type("");                       /* Enter: the rest at once              */
     type("C");
     CHECK(sent(KWNONE) == 1, "raw: (the search ran)");
     CHECK(count("line 100 of") == 1, "raw: (the feed ran)");
     CHECK(cooked == 0, "raw: paging, search, slow page and feed");
     vw_close();
     CHECK(tswidth == 132, "raw: leaving restores the word wrap width");
     CHECK(pausing, "raw: and the page pauses");
     remove("T_BODY.TXT");
     remove("T_HEAD.TXT");
}

INT
main(VOID)
{
     t_raw_output();
     t_header_pinned();
     t_header_pinned_feed();
     t_header_each_page();
     t_header_too_tall();
     t_header_height_by_hand();
     t_header_missing();
     t_continuous_carries_on();
     t_search_finds_right_page();
     t_feed_normal();
     t_feed_quiet();
     t_feed_return();
     t_one_page_prompt_once();
     t_next_page_prompt_once();
     t_search_message_once();
     t_slow_page_prompt_once();
     printf("%d checks, %d failed\n", checks, fails);
     return fails == 0 ? 0 : 1;
}

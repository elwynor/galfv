/* Build 26.10.21.2 10:40AM */
/*****************************************************************************
 *   FVPAGE.C                                    File Viewer - Page builder   *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   Turns the bytes of a file into what the user's screen should receive:  *
 *   pg_build() reads from a file offset and produces up to N screen lines,  *
 *   handling line endings, word wrap, ANSI escape sequences and IBM PC      *
 *   characters, and reports where the next page starts.  Also here: the     *
 *   content-type guess and the keyword matcher used by search.              *
 *                                                                           *
 *   This file makes no calls into the BBS beyond a few string helpers, so   *
 *   the test program in TESTS/ compiles it on its own.  See FVVIEW.C for    *
 *   how the content types differ.                                           *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVDEFS.H"

#define V        (&fvv->u.v)         /* this user's viewer state             */
#define CH_ESC   0x1B
#define CTRLZ    0x1A

/* ------------------------------------------------------------------------ *
 * IBM PC (code page 437) characters 128-255 as ASCII, for callers whose     *
 * terminals cannot show them: accents dropped, line drawing as + - | = and *
 * solid blocks as #.                                                       *
 * ------------------------------------------------------------------------ */
static const CHAR cp437[] =
     "CueaaaaceeeiiiAA"              /* 80-8F                                */
     "EaAooouuyOUcLYPf"              /* 90-9F                                */
     "aiounNao?--24!<>"              /* A0-AF                                */
     "###|++++++|+++++"              /* B0-BF  shades, single/double lines   */
     "++++-++++++++=++"              /* C0-CF                                */
     "+++++++++++#####"              /* D0-DF  blocks                        */
     "aBGpSsmtFTOd8fe^"              /* E0-EF  Greek and maths               */
     "=+><()/~o..vn2# ";             /* F0-FF                                */
typedef CHAR cp437_chk[(sizeof(cp437) == 129) ? 1 : -1];

/* ------------------------------------------------------------------------ *
 * The page builder.                                                        *
 * ------------------------------------------------------------------------ */

INT pg_lines;                        /* last build: lines completed, and     */
GBOOL pg_part;                       /*   whether a last line has no ending  */
                                     /*   (used to measure a header)         */

struct bld {                         /* working state for one build() call   */
     CHAR *buf;                      /*   output                             */
     INT n;                          /*   bytes in buf                       */
     INT lines;                      /*   complete lines so far              */
     INT maxlines;                   /*   stop after this many (0 = never)   */
     INT col;                        /*   column of the next character       */
     INT lastsp;                     /*   buf index of the last space on     */
                                     /*   this line, or -1 (text only)       */
     LONG spofs;                     /*   file offset just after that space  */
     GBOOL pend;                     /*   line full; next char wraps (ANSI)  */
};

static GBOOL
full(struct bld *b)                  /* has the page reached its line limit? */
{
     return b->maxlines > 0 && b->lines >= b->maxlines;
}

static VOID
newline(struct bld *b)
{
     b->buf[b->n++] = '\n';
     b->lines++;
     b->col = 0;
     b->lastsp = -1;
     b->pend = FALSE;
}

static GBOOL                         /*   TRUE if the page is now full       */
bput(                                /* add one printable character          */
struct bld *b,
CHAR c,
LONG tokpos,                         /*   file offset of this character      */
LONG pos,                            /*   file offset just after it          */
LONG *endpos)                        /*   where the next page starts, if full*/
{
     if (V->type == FVT_TEXT) {      /* word wrap                            */
          if (b->col >= V->wrapw) {
               if (c == ' ') {       /* a space at the margin: the break     */
                    newline(b);
                    if (full(b)) {
                         *endpos = pos;
                         return TRUE;
                    }
                    return FALSE;
               }
               if (b->lastsp >= 0) { /* break at the last space              */
                    INT sp = b->lastsp;

                    b->buf[sp] = '\n';
                    b->lines++;
                    b->col = b->n - sp - 1;
                    b->lastsp = -1;
                    if (full(b)) {   /* the carried word starts next page    */
                         b->n = sp + 1;
                         *endpos = b->spofs;
                         return TRUE;
                    }
               }
               else {                /* one long word: cut it                */
                    newline(b);
                    if (full(b)) {
                         *endpos = tokpos;
                         return TRUE;
                    }
               }
          }
          if (c == ' ') {
               b->lastsp = b->n;
               b->spofs = pos;
          }
          b->buf[b->n++] = c;
          b->col++;
     }
     else {                          /* ANSI and IBM: the terminal wraps     */
          if (b->pend) {
               b->pend = FALSE;
               b->lines++;
               b->col = 0;
               if (full(b)) {
                    *endpos = tokpos;
                    return TRUE;
               }
          }
          b->buf[b->n++] = c;
          if (++b->col >= V->termw) {
               b->pend = TRUE;
          }
     }
     return FALSE;
}

INT                                  /*   bytes placed in buf                */
pg_build(                            /* make screen output from the file     */
LONG start,                          /*   file offset to start at            */
CHAR *buf,                           /*   output, FV_PGBUF bytes             */
INT maxlines,                        /*   stop after this many lines         */
GBOOL ffbreak,                       /*   form feed ends the page?           */
LONG *endpos,                        /*   OUT: where the next part starts    */
GBOOL *eof)                          /*   OUT: nothing after this part       */
{
     FILE *fp = fvfp[usrnum];
     struct bld b;
     LONG pos = start, tokpos;
     GBOOL keepesc = (V->type == FVT_ANSI && V->ansi);
     GBOOL ascii = (!V->ansi && V->type != FVT_TEXT);
     CHAR seq[40];
     INT c, d, sl, k;
     GBOOL bad;

     setmem(&b, sizeof(b), 0);
     b.buf = buf;
     b.maxlines = maxlines;
     b.lastsp = -1;
     *eof = FALSE;
     *endpos = -1L;
     fseek(fp, start, SEEK_SET);
     for (;;) {
          if (full(&b) || b.n >= FV_PGBUF - (INT)sizeof(seq) - 8) {
               break;                /* page done, or no room for more       */
          }
          tokpos = pos;
          if ((c = getc(fp)) == EOF || c == CTRLZ) {
               *eof = TRUE;
               break;
          }
          pos++;
          if (c == CH_ESC) {         /* an escape sequence: zero width       */
               sl = 0;
               bad = FALSE;
               seq[sl++] = (CHAR)c;
               if ((d = getc(fp)) != EOF) {
                    pos++;
                    seq[sl++] = (CHAR)d;
                    if (d == '[') {  /* CSI: parameters up to a final byte   */
                         while ((d = getc(fp)) != EOF) {
                              pos++;
                              if (sl < (INT)sizeof(seq)) {
                                   seq[sl++] = (CHAR)d;
                              }
                              else {
                                   bad = TRUE;
                              }
                              if (d >= 0x40 && d <= 0x7E) {
                                   break;
                              }
                         }
                    }
               }
               if (keepesc && !bad) {
                    movmem(seq, buf + b.n, sl);
                    b.n += sl;
               }
               continue;
          }
          if (c == '\r' || c == '\n') {        /* CR, LF or CRLF: one line   */
               if (c == '\r') {
                    if ((d = getc(fp)) == '\n') {
                         pos++;
                    }
                    else if (d != EOF) {
                         ungetc(d, fp);
                    }
               }
               newline(&b);
               if (full(&b)) {
                    *endpos = pos;
               }
               continue;
          }
          if (c == '\f') {           /* form feed                            */
               if (!ffbreak) {
                    newline(&b);
               }
               else if (b.n > 0) {
                    *endpos = pos;   /* a new page starts after it           */
                    break;
               }
               continue;
          }
          if (c == '\t') {
               k = 8 - (b.col % 8);
               while (k-- > 0) {
                    if (bput(&b, ' ', tokpos, pos, endpos)) {
                         break;
                    }
               }
               if (*endpos >= 0) {
                    break;
               }
               continue;
          }
          if (c < ' ' || c == 0x7F) {
               continue;             /* other control characters: dropped    */
          }
          if (c >= 0x80 && ascii) {
               c = cp437[c - 0x80];
          }
          if (bput(&b, (CHAR)c, tokpos, pos, endpos)) {
               break;
          }
     }
     if (*endpos < 0) {
          *endpos = pos;
     }
     if (!*eof) {                    /* only blank lines left?  then end.    */
          fseek(fp, *endpos, SEEK_SET);
          do {
               c = getc(fp);
          } while (c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == '\f');
          if (c == EOF || c == CTRLZ) {
               *eof = TRUE;
          }
     }
     pg_lines = b.lines;
     pg_part = (b.col > 0 || b.pend);
     buf[b.n] = '\0';
     return b.n;
}

/* ------------------------------------------------------------------------ *
 * Content type detection.                                                  *
 * ------------------------------------------------------------------------ */

SHORT
vw_detect(const CHAR *path)          /* guess FVT_xxx from name and content  */
{
     CHAR ext[8];
     UCHAR buf[2048];
     FILE *fp;
     size_t n, i;
     INT box = 0;
     GBOOL esc = FALSE;

     fileparts(GCPART_EXTN, path, ext, sizeof(ext));
     if (sameas(ext, "ANS") || sameas(ext, ".ANS")) {
          return FVT_ANSI;
     }
     if ((fp = fopen(path, "rb")) == NULL) {
          return FVT_TEXT;
     }
     n = fread(buf, 1, sizeof(buf), fp);
     fclose(fp);
     for (i = 0 ; i < n ; i++) {
          if (buf[i] == CH_ESC && i + 1 < n && buf[i + 1] == '[') {
               esc = TRUE;
          }
          else if (buf[i] >= 0xB0 && buf[i] <= 0xDF) {
               box++;                /* IBM line-drawing and block chars     */
          }
     }
     if (esc) {
          return FVT_ANSI;
     }
     if (box >= 4 || sameas(ext, "ASC") || sameas(ext, ".ASC")
      || sameas(ext, "NFO") || sameas(ext, ".NFO")
      || sameas(ext, "DIZ") || sameas(ext, ".DIZ")) {
          return FVT_IBM;
     }
     return FVT_TEXT;
}

INT                                  /*   keyword index, or -1               */
pg_match(const CHAR *s)              /* does a page contain a keyword?       */
{
     CHAR word[FV_KWSIZ];
     INT n, i;

     while (*s != '\0') {
          if (*s == CH_ESC) {        /* skip escape sequences                */
               s++;
               if (*s == '[') {
                    s++;
                    while (*s != '\0' && !(*s >= 0x40 && *s <= 0x7E)) {
                         s++;
                    }
               }
               if (*s != '\0') {
                    s++;
               }
               continue;
          }
          if (!isalnum((UCHAR)*s)) {
               s++;
               continue;
          }
          for (n = 0 ; isalnum((UCHAR)*s) ; s++) {
               if (n < FV_KWSIZ - 1) {
                    word[n++] = *s;
               }
          }
          word[n] = '\0';
          for (i = 0 ; i < V->nkw ; i++) {
               if (sameas(word, V->kwds[i])) {
                    return i;
               }
          }
     }
     return -1;
}


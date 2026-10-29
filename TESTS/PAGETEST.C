/* Build 26.10.20.1 12:30PM */
/*****************************************************************************
 *   PAGETEST.C                           File Viewer - Page builder tests    *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   A stand-alone Windows console program that runs the page builder       *
 *   (SRC/GALFV/FVPAGE.C) against small test files and checks what it       *
 *   produces: line breaks, word wrap, page boundaries, ANSI and IBM         *
 *   handling, keyword matching and content-type detection.  It is built     *
 *   with the SDK headers but without the BBS: the few engine routines the   *
 *   builder uses are supplied below.                                        *
 *                                                                           *
 *   Build and run with TOOLS/RUNTESTS.PY.  Exit code 0 = every check        *
 *   passed.                                                                 *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVDEFS.H"

#undef printf                        /* the SDK maps it to its own gprintf  */

/* ---- Stand-ins for the engine ------------------------------------------- */

INT usrnum = 0;
CHAR *vdaptr;
FILE *fvfp[MAXNTERM];

GBOOL
sameas(const CHAR *a, const CHAR *b)
{
     return _stricmp(a, b) == 0;
}

CHAR *
fileparts(SHORT part, const CHAR *full, CHAR *dst, SHORT size)
{
     const CHAR *dot = strrchr(full, '.');

     /* Only GCPART_EXTN is used by the builder: the extension, no dot.    */
     dst[0] = '\0';
     if (part == GCPART_EXTN && dot != NULL) {
          strncpy(dst, dot + 1, size - 1);
          dst[size - 1] = '\0';
     }
     return dst;
}

/* ---- Test helpers -------------------------------------------------------- */

static struct fvvda vda;
#define V (&vda.u.v)

static INT fails = 0, checks = 0;

#define CHECK(cond, what)                                                  \
     do {                                                                  \
          checks++;                                                        \
          if (!(cond)) {                                                   \
               fails++;                                                    \
               printf("FAIL line %d: %s\n", __LINE__, what);               \
          }                                                                \
     } while (0)

static CHAR out[FV_PGBUF + 1];
static LONG endpos;
static GBOOL ateof;

static VOID
setfile(const CHAR *data, size_t len)  /* make data the file being viewed */
{
     if (fvfp[0] != NULL) {
          fclose(fvfp[0]);
     }
     fvfp[0] = tmpfile();
     fwrite(data, 1, len, fvfp[0]);
     fflush(fvfp[0]);
}
#define SETFILE(s) setfile((s), sizeof(s) - 1)

static VOID
viewer(SHORT type, SHORT ansi, SHORT lines)
{
     memset(&vda, 0, sizeof(vda));
     vdaptr = (CHAR *)&vda;
     V->type = type;
     V->ansi = ansi;
     V->wrapw = 78;
     V->termw = 80;
     V->lines = lines;
}

static INT
build(LONG start, INT maxlines, GBOOL ffbreak)
{
     return pg_build(start, out, maxlines, ffbreak, &endpos, &ateof);
}

static INT
count(const CHAR *s, CHAR c)
{
     INT n = 0;

     for ( ; *s != '\0' ; s++) {
          n += (*s == c);
     }
     return n;
}

static INT
longest(const CHAR *s)               /* longest line, escape sequences too  */
{
     INT best = 0, cur = 0;

     for ( ; *s != '\0' ; s++) {
          if (*s == '\n') {
               cur = 0;
          }
          else if (++cur > best) {
               best = cur;
          }
     }
     return best;
}

/* ---- The tests ----------------------------------------------------------- */

static VOID
t_line_endings(VOID)
{
     viewer(FVT_TEXT, 1, 22);
     SETFILE("one\r\ntwo\rthree\nfour\r\n");
     build(0, 22, TRUE);
     CHECK(strcmp(out, "one\ntwo\nthree\nfour\n") == 0,
           "CRLF, CR and LF each end exactly one line");
     CHECK(ateof, "a short file is the last page");
}

static VOID
t_page_boundary(VOID)
{
     CHAR buf[2000];
     INT i, n;
     LONG p1;

     viewer(FVT_TEXT, 1, 22);
     buf[0] = '\0';
     for (i = 1 ; i <= 30 ; i++) {
          sprintf(buf + strlen(buf), "line %02d\r\n", i);
     }
     setfile(buf, strlen(buf));
     n = build(0, 22, TRUE);
     CHECK(count(out, '\n') == 22, "page 1 has exactly 22 lines");
     CHECK(!ateof, "page 1 is not the last page");
     CHECK(strncmp(out, "line 01\n", 8) == 0, "page 1 starts at line 1");
     CHECK(endpos == 22 * 9, "page 2 starts right after line 22's CR LF");
     p1 = endpos;
     build(p1, 22, TRUE);
     CHECK(strncmp(out, "line 23\n", 8) == 0, "page 2 starts at line 23");
     CHECK(count(out, '\n') == 8, "page 2 has the remaining 8 lines");
     CHECK(ateof, "page 2 is the last page");
     (VOID)n;
}

static VOID
t_trailing_blank(VOID)
{
     viewer(FVT_TEXT, 1, 2);
     SETFILE("a\r\nb\r\n\r\n\r\n");
     build(0, 2, TRUE);
     CHECK(ateof, "only blank lines after a full page: no empty extra page");
}

static VOID
t_word_wrap(VOID)
{
     CHAR line[200];
     INT i;

     viewer(FVT_TEXT, 1, 22);
     line[0] = '\0';
     for (i = 0 ; i < 20 ; i++) {
          strcat(line, "word ");     /* 100 characters, no line break      */
     }
     strcat(line, "\r\n");
     setfile(line, strlen(line));
     build(0, 22, TRUE);
     CHECK(longest(out) <= 78, "text wraps within 78 columns");
     CHECK(count(out, '\n') == 2, "100 characters of words make 2 lines");
     CHECK(strstr(out, "wor\n") == NULL && strstr(out, "\nd ") == NULL,
           "no word is split by the wrap");
}

static VOID
t_wrap_carry_to_next_page(VOID)
{
     CHAR line[200];
     INT i;

     /* One page of one line: the word that does not fit must start page 2, */
     /* not be lost or shown twice.                                          */
     viewer(FVT_TEXT, 1, 1);
     line[0] = '\0';
     for (i = 0 ; i < 15 ; i++) {
          strcat(line, "abcdef ");   /* 105 characters                      */
     }
     strcat(line, "END");
     setfile(line, strlen(line));
     build(0, 1, TRUE);
     CHECK(count(out, '\n') == 1, "page 1 holds one line");
     CHECK(longest(out) <= 78, "page 1's line fits");
     CHECK(line[endpos] == 'a', "page 2 starts at the start of a word");
     CHECK(endpos > 0 && line[endpos - 1] == ' ',
           "page 2 starts just after the break space");
     build(endpos, 1, TRUE);
     CHECK(strstr(out, "END") != NULL, "the rest of the text is on page 2");
}

static VOID
t_long_word(VOID)
{
     CHAR line[200];

     viewer(FVT_TEXT, 1, 22);
     memset(line, 'x', 150);
     strcpy(line + 150, "\r\n");
     setfile(line, strlen(line));
     build(0, 22, TRUE);
     CHECK(longest(out) == 78, "a word longer than the screen is cut at 78");
     CHECK(count(out, 'x') == 150, "and none of it is lost");
}

static VOID
t_form_feed(VOID)
{
     viewer(FVT_TEXT, 1, 22);
     SETFILE("page one\r\n\fpage two\r\n");
     build(0, 22, TRUE);
     CHECK(strcmp(out, "page one\n") == 0, "a form feed ends the page");
     CHECK(!ateof, "and more follows");
     build(endpos, 22, TRUE);
     CHECK(strcmp(out, "page two\n") == 0, "the next page starts after it");
     build(0, 22, FALSE);
     CHECK(strcmp(out, "page one\n\npage two\n") == 0,
           "continuous mode shows a form feed as a line break");
}

static VOID
t_ctrl_z(VOID)
{
     viewer(FVT_ANSI, 1, 22);
     SETFILE("art\r\n\x1ASAUCE00 title junk");
     build(0, 22, TRUE);
     CHECK(strcmp(out, "art\n") == 0, "Ctrl-Z ends the file (SAUCE hidden)");
     CHECK(ateof, "Ctrl-Z is the end of the file");
}

static VOID
t_ansi(VOID)
{
     viewer(FVT_ANSI, 1, 22);
     SETFILE("\x1B[1;31mRed\x1B[0m\r\n");
     build(0, 22, TRUE);
     CHECK(strcmp(out, "\x1B[1;31mRed\x1B[0m\n") == 0,
           "ANSI caller: escape sequences kept");

     viewer(FVT_ANSI, 0, 22);
     SETFILE("\x1B[1;31mRed\x1B[0m \xC4\xC4\xB3\xDB\r\n");
     build(0, 22, TRUE);
     CHECK(strcmp(out, "Red --|#\n") == 0,
           "no-ANSI caller: sequences removed, line drawing as ASCII");

     viewer(FVT_TEXT, 1, 22);
     SETFILE("\x1B[1mBold\x1B[0m \xE9t\xE9\r\n");
     build(0, 22, TRUE);
     CHECK(strcmp(out, "Bold \xE9t\xE9\n") == 0,
           "text type: sequences removed, other characters untouched");
}

static VOID
t_ansi_columns(VOID)
{
     CHAR line[300];

     /* Exactly 80 characters then CR LF: the terminal is at the last       */
     /* column, so the CR LF takes it to the next line - one line, not two. */
     viewer(FVT_ANSI, 1, 22);
     memset(line, '=', 80);
     strcpy(line + 80, "\r\nnext\r\n");
     setfile(line, strlen(line));
     build(0, 2, TRUE);
     CHECK(count(out, '\n') == 2 && strstr(out, "next") != NULL,
           "an 80-column line plus CR LF is one screen line");

     /* 100 characters: the terminal wraps it onto two screen lines.         */
     viewer(FVT_ANSI, 1, 22);
     memset(line, '#', 100);
     strcpy(line + 100, "\r\nafter\r\n");
     setfile(line, strlen(line));
     build(0, 2, TRUE);
     CHECK(strstr(out, "after") == NULL,
           "a 100-column ANSI line fills two of the page's lines");

     /* Escape sequences have no width.                                     */
     viewer(FVT_ANSI, 1, 22);
     strcpy(line, "\x1B[1;37m\x1B[44m");
     memset(line + strlen(line), '-', 79);
     line[13 + 79] = '\0';
     strcat(line, "\r\nnext\r\n");
     setfile(line, strlen(line));
     build(0, 2, TRUE);
     CHECK(strstr(out, "next") != NULL, "escape sequences take no columns");
}

static VOID
t_controls_and_percent(VOID)
{
     viewer(FVT_TEXT, 1, 22);
     setfile("a\0b\x01" "c\x07" "100% d\tx\r\n", 15);
     build(0, 22, TRUE);
     CHECK(strcmp(out, "abc100% d       x\n") == 0,
           "NUL, ^A and BEL dropped; % kept; tab to column 8");
}

static VOID
t_buffer_limit(VOID)
{
     static CHAR big[60000];
     INT i, n;

     /* Thousands of colour codes and no line breaks must never overflow    */
     /* the page buffer.                                                    */
     viewer(FVT_ANSI, 1, 22);
     n = 0;
     for (i = 0 ; i < 5000 ; i++) {
          n += sprintf(big + n, "\x1B[3%dm", i % 8);
          if (i % 50 == 0) {
               big[n++] = 'x';
          }
     }
     setfile(big, n);
     n = build(0, 22, TRUE);
     CHECK(n < FV_PGBUF, "output stays inside the page buffer");
     CHECK(endpos > 0 && endpos < (LONG)strlen(big),
           "the page stops part way and says where to go on");
}

static VOID
t_match(VOID)
{
     viewer(FVT_TEXT, 1, 22);
     strcpy(V->kwds[0], "dragon");
     strcpy(V->kwds[1], "Sword");
     V->nkw = 2;
     CHECK(pg_match("The red DRAGON sleeps.") == 0, "whole word, any case");
     CHECK(pg_match("a sword, a shield") == 1, "punctuation ends a word");
     CHECK(pg_match("dragons and swordsmen") == -1, "no partial words");
     CHECK(pg_match("\x1B[1;31mdragon\x1B[0m") == 0,
           "a word inside colour codes is found");
     CHECK(pg_match("\x1B[31;1mz") == -1, "escape codes are not words");
}

static VOID
writefile(const CHAR *name, const CHAR *data, size_t len)
{
     FILE *fp = fopen(name, "wb");

     fwrite(data, 1, len, fp);
     fclose(fp);
}

static VOID
t_detect(VOID)
{
     writefile("T_PLAIN.TXT", "Just words.\r\n", 13);
     writefile("T_COLOR.TXT", "\x1B[1mHi\x1B[0m\r\n", 13);
     writefile("T_BOXES.TXT", "\xC9\xCD\xCD\xBB\r\n\xC8\xCD\xCD\xBC\r\n", 12);
     writefile("T_NAMED.ANS", "plain\r\n", 7);
     CHECK(vw_detect("T_PLAIN.TXT") == FVT_TEXT, "plain text is TEXT");
     CHECK(vw_detect("T_COLOR.TXT") == FVT_ANSI, "ESC [ in the file is ANSI");
     CHECK(vw_detect("T_BOXES.TXT") == FVT_IBM, "line drawing is IBM");
     CHECK(vw_detect("T_NAMED.ANS") == FVT_ANSI, ".ANS is ANSI");
     remove("T_PLAIN.TXT");
     remove("T_COLOR.TXT");
     remove("T_BOXES.TXT");
     remove("T_NAMED.ANS");
}

INT
main(VOID)
{
     t_line_endings();
     t_page_boundary();
     t_trailing_blank();
     t_word_wrap();
     t_wrap_carry_to_next_page();
     t_long_word();
     t_form_feed();
     t_ctrl_z();
     t_ansi();
     t_ansi_columns();
     t_controls_and_percent();
     t_buffer_limit();
     t_match();
     t_detect();
     printf("%d checks, %d failed\n", checks, fails);
     return fails == 0 ? 0 : 1;
}

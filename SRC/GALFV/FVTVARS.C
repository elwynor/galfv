/* Build 26.10.20.1 12:30PM */
/*****************************************************************************
 *   FVTVARS.C                                 File Viewer - Text variables   *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   Every changing value in GALFV.MSG (a document name, a page number, a    *
 *   setting) is a named text variable - the byte sequence                   *
 *   0x01 <justify> <width+32> NAME 0x01 - rather than a printf-style %s.    *
 *   That lets sysops reword, reorder or drop values when they edit the      *
 *   text.  The engine replaces each variable with the string returned by    *
 *   the routine registered for its name.  We fill those strings with        *
 *   tv_set() just before printing a message.                                *
 *                                                                           *
 *   prfmsg() expands variables FIRST and then runs the result through       *
 *   vsprintf() as a format string, so a '%' inside a value (a title a user  *
 *   typed) would be read as a format code.  Values are therefore returned   *
 *   with every '%' doubled.                                                 *
 *                                                                           *
 *   Names are global to the whole BBS, so all of ours start with GALFV_.    *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVDEFS.H"

#define TVBSIZ 130                   /* room for any value we show           */

static CHAR tvval[TV_COUNT_OF][TVBSIZ];        /* values as set              */
static CHAR tvout[2 * TVBSIZ];                 /* value as returned          */

static CHAR *
tvget(INT which)                     /* return a value, escaped for prfmsg() */
{
     const CHAR *s = tvval[which];
     CHAR *d = tvout;

     while (*s != '\0') {
          if (*s == '%') {
               *d++ = '%';
          }
          *d++ = *s++;
     }
     *d = '\0';
     return tvout;
}

/* The engine needs a separate routine for each registered name.            */
static CHAR *tvDOC(VOID)     { return tvget(TV_DOC);     }
static CHAR *tvTITLE(VOID)   { return tvget(TV_TITLE);   }
static CHAR *tvPAGE(VOID)    { return tvget(TV_PAGE);    }
static CHAR *tvPCT(VOID)     { return tvget(TV_PCT);     }
static CHAR *tvNUM(VOID)     { return tvget(TV_NUM);     }
static CHAR *tvITEM(VOID)    { return tvget(TV_ITEM);    }
static CHAR *tvLABEL(VOID)   { return tvget(TV_LABEL);   }
static CHAR *tvVALUE(VOID)   { return tvget(TV_VALUE);   }
static CHAR *tvLIMIT(VOID)   { return tvget(TV_LIMIT);   }
static CHAR *tvFILE(VOID)    { return tvget(TV_FILE);    }
static CHAR *tvWORD(VOID)    { return tvget(TV_WORD);    }
static CHAR *tvCOUNT(VOID)   { return tvget(TV_COUNT);   }
static CHAR *tvOWNER(VOID)   { return tvget(TV_OWNER);   }
static CHAR *tvBAUD(VOID)    { return tvget(TV_BAUD);    }
static CHAR *tvVERSION(VOID) { return GALFV_VERSION;     }

VOID
tv_init(VOID)                        /* register all of our text variables   */
{
     register_textvar("GALFV_DOC",     tvDOC);
     register_textvar("GALFV_TITLE",   tvTITLE);
     register_textvar("GALFV_PAGE",    tvPAGE);
     register_textvar("GALFV_PCT",     tvPCT);
     register_textvar("GALFV_NUM",     tvNUM);
     register_textvar("GALFV_ITEM",    tvITEM);
     register_textvar("GALFV_LABEL",   tvLABEL);
     register_textvar("GALFV_VALUE",   tvVALUE);
     register_textvar("GALFV_LIMIT",   tvLIMIT);
     register_textvar("GALFV_FILE",    tvFILE);
     register_textvar("GALFV_WORD",    tvWORD);
     register_textvar("GALFV_COUNT",   tvCOUNT);
     register_textvar("GALFV_OWNER",   tvOWNER);
     register_textvar("GALFV_BAUD",    tvBAUD);
     register_textvar("GALFV_VERSION", tvVERSION);
}

VOID
tv_set(INT which, const CHAR *value)           /* set a text value           */
{
     stzcpy(tvval[which], value, TVBSIZ);
}

VOID
tv_setn(INT which, LONG value)                 /* set a numeric value        */
{
     sprintf(tvval[which], "%ld", value);
}

/* Build 26.10.21.5 08:20PM */
/*****************************************************************************
 *   GALFV.C   v1.3.3                   File Viewer for The Major BBS v10    *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *   Originally (C) Copyright 1995 High Velocity Software, Inc.              *
 *   File Viewer converted for The Major BBS v10 by Mark Laudenbach -        *
 *   https://github.com/laudenbachm                                          *
 *                                                                           *
 *   Module entry point.  Registers the module with the BBS, opens its       *
 *   files, and routes each line a user types to the part of the module     *
 *   that is waiting for it.                                                 *
 *                                                                           *
 *   Source map:                                                             *
 *     GALFV.C    this file - startup, shutdown, input routing, hang-up      *
 *     FVLIB.C    arriving from the menu (direct links), the library list    *
 *     FVVIEW.C   the viewer: pages, continuous feed, search, baud rate      *
 *     FVEDIT.C   adding, changing and deleting documents; upload; editor    *
 *     FVSYSOP.C  the online settings editor                                 *
 *     FVSET.C    live settings (GALFVSET.DAT) and their defaults            *
 *     FVDOC.C    the document library (GALFVDOC.DAT)                        *
 *     FVTVARS.C  named text variables used in GALFV.MSG                     *
 *     GALFV.H    message numbers for GALFV.MSG                              *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0 - see the     *
 *   LICENSE file in the project root.                                       *
 *****************************************************************************/

#include <stdio.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

static GBOOL fvinput(VOID);
static VOID  fvhangup(VOID);
static VOID  fvshutdown(VOID);

static struct module galfv = {       /* our entry in the BBS module table    */
     "",                             /*   name (filled from GALFV.MDF)       */
     NULL,                           /*   logon                              */
     fvinput,                        /*   a line of input while in module    */
     NULL,                           /*   status input (engine default)      */
     NULL,                           /*   inject-other-user message          */
     NULL,                           /*   logoff                             */
     fvhangup,                       /*   hang-up                            */
     NULL,                           /*   midnight cleanup                   */
     NULL,                           /*   account deleted                    */
     fvshutdown                      /*   system shutdown                    */
};

HMCVFILE fvmb;                       /* GALFV.MCV, our message file          */
INT fvstt;                           /* our module (state) number            */

/* The open file for each channel.  Kept outside the per-user work area     */
/* because the engine clears that area whenever a user enters a module,     */
/* which would lose track of a file left open.                              */
FILE *fvfp[MAXNTERM];

static ULONG guardsv[MAXNTERM];      /* the user's own NOINJO/NOGLOB bits    */

VOID EXPORT
init__galfv(VOID)                    /* called once by the BBS at startup    */
{
     stzcpy(galfv.descrp, gmdnam("GALFV.MDF"), MNMSIZ);
     fvstt = register_module(&galfv);
     fvmb = opnmsg("GALFV.MCV");
     dclvda(sizeof(struct fvvda));   /* per-user work area we need           */
     tv_init();
     set_open();                     /* settings: load, or seed defaults     */
     doc_open();

     /* Name the running build in the Audit Trail so a sysop can always    */
     /* tell which version is loaded.                                       */
     shocst(spr("FILE VIEWER v%s", GALFV_VERSION),
            "(C) Elwynor Technologies; orig. High Velocity Software");
}

VOID
fv_guard(GBOOL on)                   /* while a file is open: no interruptions*/
{
     /* Pages from other users and global commands would scribble over a   */
     /* page, and a global command could take the user to another module    */
     /* with our file still open.  The original set these flags too; this   */
     /* version restores whatever the user had before, rather than toggling */
     /* them.                                                                */
     if (on) {
          guardsv[usrnum] = usrptr->flags & (NOINJO | NOGLOB);
          usrptr->flags |= NOINJO | NOGLOB;
     }
     else {
          usrptr->flags = (usrptr->flags & ~(NOINJO | NOGLOB)) | guardsv[usrnum];
     }
}

static GBOOL
fvinput(VOID)                        /* a line of input while in the module  */
{
     GBOOL more;
     INT st = usrptr->substt;

     setmbk(fvmb);
     if (st == ST_ENTRY) {           /* just arrived                         */
          vw_hangup(usrnum);         /* (nothing should be open; be sure)    */
          more = lib_entry();
     }
     else if (st >= ST_ASKMODE && st <= ST_DONE) {
          more = vw_input();
     }
     else if (st >= ST_ADDNAME && st < ST_SETPAGE) {
          more = ed_input();
     }
     else if (st >= ST_SETPAGE) {
          more = sy_input();
     }
     else {
          more = lib_input();
     }
     if (!more) {
          vw_close();                /* leaving: never leave a file open     */
     }
     fv_out();
     rstmbk();
     return more;                    /* FALSE returns the user to the menu   */
}

static VOID
fvhangup(VOID)                       /* a channel hung up (any module)       */
{
     vw_hangup(usrnum);
}

static VOID
fvshutdown(VOID)                     /* the BBS is shutting down             */
{
     INT i;

     for (i = 0 ; i < MAXNTERM ; i++) {
          if (fvfp[i] != NULL) {
               fclose(fvfp[i]);
               fvfp[i] = NULL;
          }
     }
     doc_close();
     set_close();
     clsmsg(fvmb);
}

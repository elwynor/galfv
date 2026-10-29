/* Build 26.10.20.1 12:30PM */
/*****************************************************************************
 *   FVSYSOP.C                              File Viewer - Settings editor     *
 *                                                                           *
 *   Copyright (C) 2026 Elwynor Technologies.                                *
 *                                                                           *
 *   The online settings editor, reached with S at the library prompt by     *
 *   holders of the sysop key.  Changes are saved to GALFVSET.DAT and take   *
 *   effect at once; each one is noted in the Audit Trail.                   *
 *                                                                           *
 *   Licensed under the GNU Affero General Public License v3.0.              *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "FVSET.H"

VOID
sy_settings(VOID)                    /* show the settings page               */
{
     INT i;

     prfmsg(SETHDR);
     for (i = 0 ; i < set_count() ; i++) {
          tv_setn(TV_ITEM, i + 1);
          tv_set(TV_LABEL, set_label(i));
          tv_set(TV_VALUE, set_value(i));
          prfmsg(SETROW);
     }
     prfmsg(SETPRMT);
     usrptr->substt = ST_SETPAGE;
}

GBOOL
sy_input(VOID)                       /* input in the settings editor         */
{
     CHAR line[INPSIZ];
     INT n;

     if (!lib_issysop()) {           /* the key may have just been changed   */
          return lib_home();
     }
     switch (usrptr->substt) {
     case ST_SETPAGE:
          if (margc == 0) {
               sy_settings();
               return TRUE;
          }
          if (isdigit((UCHAR)margv[0][0])) {
               n = atoi(margv[0]) - 1;
               if (n >= 0 && n < set_count()) {
                    fvv->item = n;
                    tv_set(TV_LABEL, set_label(n));
                    tv_set(TV_VALUE, set_value(n));
                    tv_set(TV_LIMIT, set_limit(n));
                    prfmsg(SETVAL);
                    usrptr->substt = ST_SETVAL;
                    return TRUE;
               }
          }
          switch (toupper((UCHAR)margv[0][0])) {
          case 'R':
               prfmsg(SETRSTQ);
               usrptr->substt = ST_SETRST;
               return TRUE;
          case 'X':
          case 'Q':
               return lib_home();
          }
          sy_settings();
          return TRUE;
     case ST_SETVAL:
          rstrin();
          stzcpy(line, skpwht(input), sizeof(line));
          unpad(line);
          switch (set_change(fvv->item, line)) {
          case SETCHG_OK:
               tv_set(TV_LABEL, set_label(fvv->item));
               tv_set(TV_VALUE, set_value(fvv->item));
               prfmsg(SETSAVED);
               shocst("GALFV SETTING CHANGED", "%s: %.30s = %.30s",
                      usaptr->userid, set_label(fvv->item),
                      set_value(fvv->item));
               break;
          case SETCHG_BAD:
               prfmsg(BADVAL);
               break;
          default:                   /* empty: unchanged                     */
               break;
          }
          sy_settings();
          return TRUE;
     case ST_SETRST:
          if (margc > 0 && toupper((UCHAR)margv[0][0]) == 'Y') {
               set_defaults();
               prfmsg(SETRSTD);
               shocst("GALFV SETTINGS RESET", "%s reset all settings",
                      usaptr->userid);
          }
          sy_settings();
          return TRUE;
     }
     return lib_home();
}

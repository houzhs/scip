/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2005 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2005 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: heur.c,v 1.54 2005/10/20 11:07:04 bzfpfend Exp $"

/**@file   heur.c
 * @brief  methods for primal heuristics
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/def.h"
#include "scip/message.h"
#include "scip/set.h"
#include "scip/clock.h"
#include "scip/paramset.h"
#include "scip/primal.h"
#include "scip/scip.h"
#include "scip/heur.h"

#include "scip/struct_heur.h"



/** compares two heuristics w. r. to their delay positions and their priority */
SCIP_DECL_SORTPTRCOMP(SCIPheurComp)
{  /*lint --e{715}*/
   SCIP_HEUR* heur1 = (SCIP_HEUR*)elem1;
   SCIP_HEUR* heur2 = (SCIP_HEUR*)elem2;

   assert(heur1 != NULL);
   assert(heur2 != NULL);

   if( heur1->delaypos == heur2->delaypos )
   {
      assert(heur1->delaypos == -1);
      assert(heur2->delaypos == -1);
      return heur2->priority - heur1->priority; /* prefer higher priorities */
   }
   else if( heur1->delaypos == -1 )
      return +1;                                /* prefer delayed heuristics */
   else if( heur2->delaypos == -1 )
      return -1;                                /* prefer delayed heuristics */
   else if( heur1->ncalls * heur1->freq > heur2->ncalls * heur2->freq )
      return +1;
   else if( heur1->ncalls * heur1->freq < heur2->ncalls * heur2->freq )
      return -1;
   else
      return heur1->delaypos - heur2->delaypos; /* prefer lower delay positions */
}

/** method to call, when the priority of a heuristic was changed */
static
SCIP_DECL_PARAMCHGD(paramChgdHeurPriority)
{  /*lint --e{715}*/
   SCIP_PARAMDATA* paramdata;

   paramdata = SCIPparamGetData(param);
   assert(paramdata != NULL);

   /* use SCIPsetHeurPriority() to mark the heurs unsorted */
   SCIP_CALL( SCIPsetHeurPriority(scip, (SCIP_HEUR*)paramdata, SCIPparamGetInt(param)) ); /*lint !e740*/

   return SCIP_OKAY;
}

/** creates a primal heuristic */
SCIP_RETCODE SCIPheurCreate(
   SCIP_HEUR**           heur,               /**< pointer to primal heuristic data structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   BMS_BLKMEM*           blkmem,             /**< block memory for parameter settings */
   const char*           name,               /**< name of primal heuristic */
   const char*           desc,               /**< description of primal heuristic */
   char                  dispchar,           /**< display character of primal heuristic */
   int                   priority,           /**< priority of the primal heuristic */
   int                   freq,               /**< frequency for calling primal heuristic */
   int                   freqofs,            /**< frequency offset for calling primal heuristic */
   int                   maxdepth,           /**< maximal depth level to call heuristic at (-1: no limit) */
   SCIP_Bool             pseudonodes,        /**< call heuristic at nodes where only a pseudo solution exist? */
   SCIP_Bool             duringplunging,     /**< call heuristic during plunging? */
   SCIP_Bool             duringlploop,       /**< call heuristic during the LP price-and-cut loop? */
   SCIP_Bool             afternode,          /**< call heuristic after or before the current node was solved? */
   SCIP_DECL_HEURFREE    ((*heurfree)),      /**< destructor of primal heuristic */
   SCIP_DECL_HEURINIT    ((*heurinit)),      /**< initialize primal heuristic */
   SCIP_DECL_HEUREXIT    ((*heurexit)),      /**< deinitialize primal heuristic */
   SCIP_DECL_HEURINITSOL ((*heurinitsol)),   /**< solving process initialization method of primal heuristic */
   SCIP_DECL_HEUREXITSOL ((*heurexitsol)),   /**< solving process deinitialization method of primal heuristic */
   SCIP_DECL_HEUREXEC    ((*heurexec)),      /**< execution method of primal heuristic */
   SCIP_HEURDATA*        heurdata            /**< primal heuristic data */
   )
{
   char paramname[SCIP_MAXSTRLEN];
   char paramdesc[SCIP_MAXSTRLEN];

   assert(heur != NULL);
   assert(name != NULL);
   assert(desc != NULL);
   assert(freq >= -1);
   assert(freqofs >= 0);
   assert(heurexec != NULL);

   SCIP_ALLOC( BMSallocMemory(heur) );
   SCIP_ALLOC( BMSduplicateMemoryArray(&(*heur)->name, name, strlen(name)+1) );
   SCIP_ALLOC( BMSduplicateMemoryArray(&(*heur)->desc, desc, strlen(desc)+1) );
   (*heur)->dispchar = dispchar;
   (*heur)->priority = priority;
   (*heur)->freq = freq;
   (*heur)->freqofs = freqofs;
   (*heur)->maxdepth = maxdepth;
   (*heur)->delaypos = -1;
   (*heur)->pseudonodes = pseudonodes;
   (*heur)->duringplunging = duringplunging;
   (*heur)->duringlploop = duringlploop;
   (*heur)->afternode = afternode;
   (*heur)->heurfree = heurfree;
   (*heur)->heurinit = heurinit;
   (*heur)->heurexit = heurexit;
   (*heur)->heurinitsol = heurinitsol;
   (*heur)->heurexitsol = heurexitsol;
   (*heur)->heurexec = heurexec;
   (*heur)->heurdata = heurdata;
   SCIP_CALL( SCIPclockCreate(&(*heur)->heurclock, SCIP_CLOCKTYPE_DEFAULT) );
   (*heur)->ncalls = 0;
   (*heur)->nsolsfound = 0;
   (*heur)->nbestsolsfound = 0;
   (*heur)->initialized = FALSE;

   /* add parameters */
   sprintf(paramname, "heuristics/%s/priority", name);
   sprintf(paramdesc, "priority of heuristic <%s>", name);
   SCIP_CALL( SCIPsetAddIntParam(set, blkmem, paramname, paramdesc,
                  &(*heur)->priority, priority, INT_MIN, INT_MAX, 
                  paramChgdHeurPriority, (SCIP_PARAMDATA*)(*heur)) ); /*lint !e740*/
   sprintf(paramname, "heuristics/%s/freq", name);
   sprintf(paramdesc, "frequency for calling primal heuristic <%s> (-1: never, 0: only at depth freqofs)", name);
   SCIP_CALL( SCIPsetAddIntParam(set, blkmem, paramname, paramdesc,
                  &(*heur)->freq, freq, -1, INT_MAX, NULL, NULL) );
   sprintf(paramname, "heuristics/%s/freqofs", name);
   sprintf(paramdesc, "frequency offset for calling primal heuristic <%s>", name);
   SCIP_CALL( SCIPsetAddIntParam(set, blkmem, paramname, paramdesc,
                  &(*heur)->freqofs, freqofs, 0, INT_MAX, NULL, NULL) );
   sprintf(paramname, "heuristics/%s/maxdepth", name);
   sprintf(paramdesc, "maximal depth level to call primal heuristic <%s> (-1: no limit)", name);
   SCIP_CALL( SCIPsetAddIntParam(set, blkmem, paramname, paramdesc,
                  &(*heur)->maxdepth, maxdepth, -1, INT_MAX, NULL, NULL) );

   return SCIP_OKAY;
}

/** calls destructor and frees memory of primal heuristic */
SCIP_RETCODE SCIPheurFree(
   SCIP_HEUR**           heur,               /**< pointer to primal heuristic data structure */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(heur != NULL);
   assert(*heur != NULL);
   assert(!(*heur)->initialized);
   assert(set != NULL);

   /* call destructor of primal heuristic */
   if( (*heur)->heurfree != NULL )
   {
      SCIP_CALL( (*heur)->heurfree(set->scip, *heur) );
   }

   SCIPclockFree(&(*heur)->heurclock);
   BMSfreeMemoryArray(&(*heur)->name);
   BMSfreeMemoryArray(&(*heur)->desc);
   BMSfreeMemory(heur);

   return SCIP_OKAY;
}

/** initializes primal heuristic */
SCIP_RETCODE SCIPheurInit(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(heur != NULL);
   assert(set != NULL);

   if( heur->initialized )
   {
      SCIPerrorMessage("primal heuristic <%s> already initialized\n", heur->name);
      return SCIP_INVALIDCALL;
   }

   SCIPclockReset(heur->heurclock);

   heur->ncalls = 0;
   heur->nsolsfound = 0;
   heur->nbestsolsfound = 0;

   if( heur->heurinit != NULL )
   {
      SCIP_CALL( heur->heurinit(set->scip, heur) );
   }
   heur->initialized = TRUE;

   return SCIP_OKAY;
}

/** calls exit method of primal heuristic */
SCIP_RETCODE SCIPheurExit(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(heur != NULL);
   assert(set != NULL);

   if( !heur->initialized )
   {
      SCIPerrorMessage("primal heuristic <%s> not initialized\n", heur->name);
      return SCIP_INVALIDCALL;
   }

   if( heur->heurexit != NULL )
   {
      SCIP_CALL( heur->heurexit(set->scip, heur) );
   }
   heur->initialized = FALSE;

   return SCIP_OKAY;
}

/** informs primal heuristic that the branch and bound process is being started */
SCIP_RETCODE SCIPheurInitsol(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(heur != NULL);
   assert(set != NULL);

   if( heur->delaypos != -1 )
   {
      heur->delaypos = -1;
      set->heurssorted = FALSE;
   }

   /* call solving process initialization method of primal heuristic */
   if( heur->heurinitsol != NULL )
   {
      SCIP_CALL( heur->heurinitsol(set->scip, heur) );
   }

   return SCIP_OKAY;
}

/** informs primal heuristic that the branch and bound process data is being freed */
SCIP_RETCODE SCIPheurExitsol(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(heur != NULL);
   assert(set != NULL);

   /* call solving process deinitialization method of primal heuristic */
   if( heur->heurexitsol != NULL )
   {
      SCIP_CALL( heur->heurexitsol(set->scip, heur) );
   }

   return SCIP_OKAY;
}

/** calls execution method of primal heuristic */
SCIP_RETCODE SCIPheurExec(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_PRIMAL*          primal,             /**< primal data */
   int                   depth,              /**< depth of current node */
   int                   lpforkdepth,        /**< depth of the last node with solved LP */
   SCIP_Bool             currentnodehaslp,   /**< is LP being processed in the current node? */
   SCIP_Bool             plunging,           /**< is the next node to be processed a child or sibling? */
   SCIP_Bool             nodesolved,         /**< is the current node already solved? */
   SCIP_Bool             inlploop,           /**< are we currently in the LP solving loop? */
   int*                  ndelayedheurs,      /**< pointer to count the number of delayed heuristics */
   SCIP_RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   SCIP_Bool execute;

   assert(heur != NULL);
   assert(heur->heurexec != NULL);
   assert(heur->freq >= -1);
   assert(heur->freqofs >= 0);
   assert(heur->maxdepth >= -1);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(primal != NULL);
   assert(depth >= 0);
   assert(ndelayedheurs != NULL);
   assert(result != NULL);

   *result = SCIP_DIDNOTRUN;

   if( heur->pseudonodes )
   {
      /* heuristic may be executed on every node: check, if the current depth matches the execution frequency and offset */
      execute = (heur->freq > 0 && depth >= heur->freqofs && (depth - heur->freqofs) % heur->freq == 0);
   }
   else
   {
      /* heuristic may only be executed on LP nodes: check, if a node matching the execution frequency lies between the
       * current node and the last LP node of the path
       */
      execute = (heur->freq > 0 && depth >= heur->freqofs
         && ((depth + heur->freq - heur->freqofs) / heur->freq
            != (lpforkdepth + heur->freq - heur->freqofs) / heur->freq));
   }

   /* if frequency is zero, execute heuristic only at the depth level of the frequency offset */
   execute = execute || (depth == heur->freqofs && heur->freq == 0);

   /* compare current depth against heuristic's maximal depth level */
   execute = execute && (heur->maxdepth == -1 || depth <= heur->maxdepth);
   
   /* if the heuristic was delayed, execute it anywas */
   execute = execute || (heur->delaypos >= 0);

   /* execute LP heuristics only at LP nodes */
   execute = execute && (heur->pseudonodes || currentnodehaslp);

   /* execute heuristic, depending on its "afternode" and "duringlploop" flags */
   execute = execute && ((heur->afternode == nodesolved && !inlploop) || (heur->duringlploop && inlploop));

   if( execute )
   {
      if( plunging && !heur->duringplunging && depth > 0 )
      {
         /* the heuristic should be delayed until plunging is finished */
         *result = SCIP_DELAYED;
      }
      else
      {
         SCIP_Longint oldnsolsfound;
         SCIP_Longint oldnbestsolsfound;
         
         SCIPdebugMessage("executing primal heuristic <%s> in depth %d (delaypos: %d)\n", heur->name, depth, heur->delaypos);

         oldnsolsfound = primal->nsolsfound;
         oldnbestsolsfound = primal->nbestsolsfound;

         /* start timing */
         SCIPclockStart(heur->heurclock, set);

         /* call external method */
         SCIP_CALL( heur->heurexec(set->scip, heur, plunging, inlploop, result) );

         /* stop timing */
         SCIPclockStop(heur->heurclock, set);

         /* evaluate result */
         if( *result != SCIP_FOUNDSOL
            && *result != SCIP_DIDNOTFIND
            && *result != SCIP_DIDNOTRUN
            && *result != SCIP_DELAYED )
         {
            SCIPerrorMessage("execution method of primal heuristic <%s> returned invalid result <%d>\n", 
               heur->name, *result);
            return SCIP_INVALIDRESULT;
         }
         if( *result != SCIP_DIDNOTRUN && *result != SCIP_DELAYED )
            heur->ncalls++;
         heur->nsolsfound += primal->nsolsfound - oldnsolsfound;
         heur->nbestsolsfound += primal->nbestsolsfound - oldnbestsolsfound;

         /* update delay position of heuristic */
         if( *result != SCIP_DELAYED && heur->delaypos != -1 )
         {
            heur->delaypos = -1;
            set->heurssorted = FALSE;
         }
      }
   }
   assert(*result == SCIP_DIDNOTRUN || *result == SCIP_DELAYED || heur->delaypos == -1);

   /* check if the heuristic was (still) delayed */
   if( *result == SCIP_DELAYED || heur->delaypos >= 0 )
   {
      SCIPdebugMessage("delaying execution of primal heuristic <%s> in depth %d (delaypos: %d)\n", 
         heur->name, depth, *ndelayedheurs);

      /* mark the heuristic delayed */
      if( heur->delaypos != *ndelayedheurs )
      {
         heur->delaypos = *ndelayedheurs;
         set->heurssorted = FALSE;
      }
      (*ndelayedheurs)++;
   }

   return SCIP_OKAY;
}

/** gets user data of primal heuristic */
SCIP_HEURDATA* SCIPheurGetData(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->heurdata;
}

/** sets user data of primal heuristic; user has to free old data in advance! */
void SCIPheurSetData(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_HEURDATA*        heurdata            /**< new primal heuristic user data */
   )
{
   assert(heur != NULL);

   heur->heurdata = heurdata;
}

/** gets name of primal heuristic */
const char* SCIPheurGetName(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->name;
}

/** gets description of primal heuristic */
const char* SCIPheurGetDesc(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->desc;
}

/** gets display character of primal heuristic */
char SCIPheurGetDispchar(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   if( heur == NULL )
      return '*';
   else
      return heur->dispchar;
}

/** gets priority of primal heuristic */
int SCIPheurGetPriority(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->priority;
}

/** sets priority of primal heuristic */
void SCIPheurSetPriority(
   SCIP_HEUR*            heur,               /**< primal heuristic */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   priority            /**< new priority of the primal heuristic */
   )
{
   assert(heur != NULL);
   assert(set != NULL);
   
   heur->priority = priority;
   set->heurssorted = FALSE;
}

/** gets frequency of primal heuristic */
int SCIPheurGetFreq(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->freq;
}

/** gets frequency offset of primal heuristic */
int SCIPheurGetFreqofs(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->freqofs;
}

/** gets maximal depth level for calling primal heuristic (returns -1, if no depth limit exists) */
int SCIPheurGetMaxdepth(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->maxdepth;
}

/** gets the number of times, the heuristic was called and tried to find a solution */
SCIP_Longint SCIPheurGetNCalls(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->ncalls;
}

/** gets the number of primal feasible solutions found by this heuristic */
SCIP_Longint SCIPheurGetNSolsFound(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->nsolsfound;
}

/** gets the number of new best primal feasible solutions found by this heuristic */
SCIP_Longint SCIPheurGetNBestSolsFound(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->nbestsolsfound;
}

/** is primal heuristic initialized? */
SCIP_Bool SCIPheurIsInitialized(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return heur->initialized;
}

/** gets time in seconds used in this heuristic */
SCIP_Real SCIPheurGetTime(
   SCIP_HEUR*            heur                /**< primal heuristic */
   )
{
   assert(heur != NULL);

   return SCIPclockGetTime(heur->heurclock);
}

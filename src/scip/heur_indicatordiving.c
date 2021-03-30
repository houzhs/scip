/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2020 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scipopt.org.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   heur_indicatordiving.c
 * @ingroup DEFPLUGINS_HEUR
 * @brief  indicator diving heuristic
 * @author Katrin Halbig
 * @author Alexander Hoen
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "scip/heur_indicatordiving.h"
#include "scip/heuristics.h"
#include "scip/pub_heur.h"
#include "scip/pub_message.h"
#include "scip/scip_heur.h"
#include "scip/scip_mem.h"
#include "scip/scip_numerics.h"
#include "scip/scip_sol.h"
#include <string.h>

#define HEUR_NAME             "indicatordiving"
#define HEUR_DESC             "indicator diving heuristic"
#define HEUR_DISPCHAR         '?' /**< todo: change to SCIP_HEURDISPCHAR_DIVING */
#define HEUR_PRIORITY         0
#define HEUR_FREQ             1
#define HEUR_FREQOFS          1
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_AFTERLPPLUNGE
#define HEUR_USESSUBSCIP      FALSE  /**< does the heuristic use a secondary SCIP instance? */
#define DIVESET_DIVETYPES     SCIP_DIVETYPE_INTEGRALITY /**< bit mask that represents all supported dive types */
#define DIVESET_ISPUBLIC      FALSE   /**< is this dive set publicly available (ie., can be used by other primal heuristics?) */


/*
 * Default parameter settings
 */

#define DEFAULT_MINRELDEPTH         0.0 /**< minimal relative depth to start diving */
#define DEFAULT_MAXRELDEPTH         1.0 /**< maximal relative depth to start diving */
#define DEFAULT_MAXLPITERQUOT      0.05 /**< maximal fraction of diving LP iterations compared to node LP iterations */
#define DEFAULT_MAXLPITEROFS       1000 /**< additional number of allowed LP iterations */
#define DEFAULT_MAXDIVEUBQUOT       0.8 /**< maximal quotient (curlowerbound - lowerbound)/(cutoffbound - lowerbound)
                                         *   where diving is performed (0.0: no limit) */
#define DEFAULT_MAXDIVEAVGQUOT      0.0 /**< maximal quotient (curlowerbound - lowerbound)/(avglowerbound - lowerbound)
                                         *   where diving is performed (0.0: no limit) */
#define DEFAULT_MAXDIVEUBQUOTNOSOL  0.1 /**< maximal UBQUOT when no solution was found yet (0.0: no limit) */
#define DEFAULT_MAXDIVEAVGQUOTNOSOL 0.0 /**< maximal AVGQUOT when no solution was found yet (0.0: no limit) */
#define DEFAULT_BACKTRACK          TRUE /**< use one level of backtracking if infeasibility is encountered? */
#define DEFAULT_LPRESOLVEDOMCHGQUOT 0.15 /**< percentage of immediate domain changes during probing to trigger LP resolve */
#define DEFAULT_LPSOLVEFREQ           0 /**< LP solve frequency for diving heuristics */
#define DEFAULT_ONLYLPBRANCHCANDS FALSE /**< should only LP branching candidates be considered instead of the slower but
                                         *   more general constraint handler diving variable selection? */
#define DEFAULT_RANDSEED            11  /**< initial seed for random number generation */

/** locally defined heuristic data */
struct SCIP_HeurData
{
};

/*
 * Local methods
 */

/* put your local methods here, and declare them static */


/*
 * Callback methods
 */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopyIndicatordiving)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurIndicatordiving(scip) );

   return SCIP_OKAY;
}


/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeIndicatordiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);

   /* free heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   SCIPfreeBlockMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitIndicatordiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   return SCIP_OKAY;
}


/** deinitialization method of primal heuristic (called before transformed problem is freed) */
static
SCIP_DECL_HEUREXIT(heurExitIndicatordiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   return SCIP_OKAY;
}


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecIndicatordiving)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of xyz primal heuristic not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}

/** calculate score and preferred rounding direction for the candidate variable */
static
SCIP_DECL_DIVESETGETSCORE(divesetGetScoreIndicatordiving)
{

   return SCIP_OKAY;
}

#define divesetAvailableIndicatordiving NULL

/*
 * heuristic specific interface methods
 */

/** creates the indicatordiving heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurIndicatordiving(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;

   /* create indicatordiving primal heuristic data */
   heurdata = NULL;

   heur = NULL;


   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecIndicatordiving, heurdata) );

   assert(heur != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetHeurCopy(scip, heur, heurCopyIndicatordiving) );
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeIndicatordiving) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitIndicatordiving) );
   SCIP_CALL( SCIPsetHeurExit(scip, heur, heurExitIndicatordiving) );

   /* create a diveset (this will automatically install some additional parameters for the heuristic)*/
   SCIP_CALL( SCIPcreateDiveset(scip, NULL, heur, HEUR_NAME, DEFAULT_MINRELDEPTH, DEFAULT_MAXRELDEPTH, DEFAULT_MAXLPITERQUOT,
         DEFAULT_MAXDIVEUBQUOT, DEFAULT_MAXDIVEAVGQUOT, DEFAULT_MAXDIVEUBQUOTNOSOL, DEFAULT_MAXDIVEAVGQUOTNOSOL, DEFAULT_LPRESOLVEDOMCHGQUOT,
         DEFAULT_LPSOLVEFREQ, DEFAULT_MAXLPITEROFS, DEFAULT_RANDSEED, DEFAULT_BACKTRACK, DEFAULT_ONLYLPBRANCHCANDS,
         DIVESET_ISPUBLIC, DIVESET_DIVETYPES, divesetGetScoreIndicatordiving, divesetAvailableIndicatordiving) );

   return SCIP_OKAY;
}

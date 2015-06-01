/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2015 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   heur_tm.h
 * @ingroup PRIMALHEURISTICS
 * @brief  TM primal heuristic
 * @author Gerald Gamrath
 * @author Thorsten Koch
 * @author Michael Winkler
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_HEUR_TM_H__
#define __SCIP_HEUR_TM_H__

#include "scip/scip.h"
#include "grph.h"
#define DEFAULT_HOPFACTOR 0.33
#ifdef __cplusplus
extern "C" {
#endif

/** creates the TM primal heuristic and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeHeurTM(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** perform shortest paths heuristic */
extern
SCIP_RETCODE do_layer(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEURDATA*        heurdata,           /**< SCIP data structure */
   const GRAPH*          graph,              /**< graph data structure */
   int*                  starts,             /**< array containing start vertices (NULL to not provide any) */
   int*                  bestnewstart,       /**< pointer to the start vertex resulting in the best soluton */
   int*                  best_result,        /**< array indicating whether an arc is part of the solution (CONNECTED/UNKNOWN) */
   SCIP_Real*            nodepriority,       /**< vertex priorities for vertices to be starting points (NULL for no priorities) */
   int                   runs,               /**< number of runs */
   int                   bestincstart,       /**< best incumbent start vertex */
   SCIP_Real*            cost,               /**< arc costs */
   SCIP_Real*            costrev,            /**< reversed arc costs */
   SCIP_Real*            hopfactor,          /**< edge cost multiplicator for HC problems */
   SCIP_Real             maxcost,            /**< maximal edge cost (only for HC) */
   SCIP_Bool*            success             /**< pointer to store whether a solution could be found */
   );

/** prune the Steiner tree in such a way that all leaves are terminals */
extern
SCIP_RETCODE do_prune(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   SCIP_Real*            cost,               /**< edge costs */
   int                   layer,              /**< layer, @note: should be set to 0 */
   int*                  result,             /**< ST edges */
   char*                 connected           /**< ST nodes */
   );

/** prune the (rooted) prize collecting Steiner tree in such a way that all leaves are terminals */
extern
SCIP_RETCODE do_pcprune(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   SCIP_Real*            cost,               /**< edge costs */
   int*                  result,             /**< ST edges */
   char*                 connected           /**< ST nodes */
   );

/** prune degree constrained Tree in such a way that all leaves are terminals */
extern
SCIP_RETCODE do_degprune(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph structure */
   int*                  result,             /**< ST edges */
   char*                 connected           /**< ST nodes */
   );

#ifdef __cplusplus
}
#endif

#endif

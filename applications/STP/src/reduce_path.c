/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2021 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scipopt.org.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   reduce_path.c
 * @brief  Path deletion reduction test for Steiner problems
 * @author Daniel Rehfeldt
 *
 * This file implements a slightly improved version of the so called "path substitution test" by Polzin and Vahdati Daneshmand
 *
 * A list of all interface methods can be found in reduce.h.
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

//#define SCIP_DEBUG

#include <assert.h>
#include "reduce.h"

#define SP_MAXNDISTS 10000
#define SP_MAXLENGTH 10
#define SP_MAXDEGREE 10
#define SP_MAXNSTARTS 10000
#define VNODES_UNSET -1
#define SP_MAXNPULLS 3


/** path replacement */
typedef struct path_replacement
{
   //DHEAP*                dheap;              /**< heap for shortest path computations */
   STP_Vectype(SCIP_Real) firstneighborcosts; /**< edge costs from tail of path to non-path neighbors */
  // STP_Vectype(SCIP_Real) currneighborcosts;  /**< edge costs from head of path to current neighbors */
   STP_Vectype(int)      currneighbors;      /**< current neighbors */
   STP_Vectype(int)      pathedges;          /**< edges of path */
   STP_Vectype(int)      visitednodes;       /**< visited nodes */
   SCIP_Real* RESTRICT   sp_dists;           /**< distances to neighbors of path start node */
   int* RESTRICT         sp_starts;          /**< CSR like starts for each node in the path, pointing to sp_dists */
   int* RESTRICT         nodes_index;        /**< maps each node to index in 0,1,2,..., or to VNODES_UNSET, VNODES_INPATH  */
   SCIP_Bool* RESTRICT   nodeindices_isPath; /**< is a node index in the path? */
   SCIP_Real             pathcost;           /**< cost of path */
   int                   nfirstneighbors;    /**< number of neighbors of first path node */
   int                   pathtail;           /**< first node of path */
   int                   failneighbor;       /**< temporary */
   int                   nnodes;             /**< number of nodes */
} PR;



/*
 * Local methods
 */


/** gets head of path */
static inline
int pathGetHead(
   const GRAPH*          g,                  /**< graph data structure */
   const PR*             pr                  /**< path replacement data structure */
   )
{
   const int npathedges = StpVecGetSize(pr->pathedges);
   return g->head[pr->pathedges[npathedges - 1]];
}

/** tries to rule out neighbors of path head */
static inline
void ruleOutFromHead(
   PR*                   pr,                 /**< path replacement data structure */
   SCIP_Bool*            needFullRuleOut
   )
{
   assert(FALSE == *needFullRuleOut);

   SCIPdebugMessage("starting head rule-out with pathcost=%f \n", pr->pathcost);

   if( pr->nfirstneighbors > 2 )
   {
      pr->failneighbor = VNODES_UNSET;
      *needFullRuleOut = TRUE;
      return;
   }
   else
   {
      const SCIP_Real* const sp_dists = pr->sp_dists;
      const SCIP_Real pathcost = pr->pathcost;
      const int ncurrneighbors = StpVecGetSize(pr->currneighbors);
      const int nfirst = pr->nfirstneighbors;
      int failneighbor = VNODES_UNSET;

      for( int i = 0; i < ncurrneighbors; i++ )
      {
         int j;
         const int neighbor = pr->currneighbors[i];
         const int sp_start = pr->sp_starts[pr->nodes_index[neighbor]];

         assert(pr->nodes_index[neighbor] >= 0);

         for( j = 0; j < nfirst; j++ )
         {
            SCIPdebugMessage("dist for neighbor=%d, idx=%d: %f \n", neighbor, j, sp_dists[sp_start + j]);
            if( LE(sp_dists[sp_start + j], pathcost) )
               break;
         }

         if( j == nfirst )
         {
            SCIPdebugMessage("neighbor %d not head-ruled-out \n", neighbor);
            if( failneighbor != VNODES_UNSET )
            {
               pr->failneighbor = VNODES_UNSET;
               *needFullRuleOut = TRUE;
               return;
            }
            failneighbor = neighbor;
         }
      }

      pr->failneighbor = failneighbor;
   }
}


/** tries to rule out neighbors of path tail */
static inline
void ruleOutFromTailSingle(
   const GRAPH*          g,                  /**< graph data structure */
   PR*                   pr,                 /**< path replacement data structure */
   SCIP_Bool*            isRuledOut
)
{
   const SCIP_Real* const sp_dists = pr->sp_dists;
   const int* const sp_starts = pr->sp_starts;
   const int pathhead = pathGetHead(g, pr);
   const int pathhead_idx = pr->nodes_index[pathhead];

   assert(!*isRuledOut);

   SCIPdebugMessage("try tail rule-out with pathcost=%f \n", pr->pathcost);

   for( int i = 0; i < pr->nfirstneighbors; i++ )
   {
      const SCIP_Real extpathcost = pr->pathcost + pr->firstneighborcosts[i];
      const SCIP_Real altpathcost = sp_dists[sp_starts[pathhead_idx] + i];

      SCIPdebugMessage("extpathcost for idx=%d: %f \n", i, extpathcost);
      SCIPdebugMessage("althpathost for idx=%d: %f \n", i, altpathcost);

      if( GT(altpathcost, extpathcost) )
      {
         SCIPdebugMessage("no rule-out for initial neighbor idx=%d \n", i);
         return;
      }
   }

   *isRuledOut = TRUE;
}


/** tries to rule out neighbors of path tail */
static inline
void ruleOutFromTailCombs(
   const GRAPH*          g,                  /**< graph data structure */
   PR*                   pr,                 /**< path replacement data structure */
   SCIP_Bool*            isRuledOut
)
{
   const SCIP_Real* const sp_dists = pr->sp_dists;
   const int* const sp_starts = pr->sp_starts;
   const int pathhead = pathGetHead(g, pr);
   const int pathhead_idx = pr->nodes_index[pathhead];
   SCIP_Bool hasFail = FALSE;

   assert(!*isRuledOut);
   SCIPdebugMessage("try full tail rule-out with pathcost=%f \n", pr->pathcost);

   for( int i = 0; i < pr->nfirstneighbors; i++ )
   {
      const SCIP_Real altpathcost = sp_dists[sp_starts[pathhead_idx] + i];

      SCIPdebugMessage("althpathost for idx=%d: %f \n", i, altpathcost);

      if( GT(altpathcost, pr->pathcost) )
      {
         SCIPdebugMessage("no full rule-out for initial neighbor idx=%d \n", i);

         if( hasFail )
         {
            SCIPdebugMessage("...no full rule-out \n");
            return;
         }

         hasFail = TRUE;
      }
   }

   *isRuledOut = TRUE;
}


/** adds new path node */
static inline
void addPathNode(
   SCIP*                 scip,               /**< SCIP */
   int                   node,               /**< node to add */
   PR*                   pr                  /**< path replacement data structure */
   )
{
   assert(pr->nodes_index[node] == VNODES_UNSET);

   pr->nodes_index[node] = StpVecGetSize(pr->visitednodes);
   StpVecPushBack(scip, pr->visitednodes, node);
   pr->nodeindices_isPath[pr->nodes_index[node]] = TRUE;
}

/** adds new NON-path node */
static inline
void addNonPathNode(
   SCIP*                 scip,               /**< SCIP */
   int                   node,               /**< node to add */
   PR*                   pr                  /**< path replacement data structure */
   )
{
   assert(pr->nodes_index[node] == VNODES_UNSET);

   pr->nodes_index[node] = StpVecGetSize(pr->visitednodes);
   StpVecPushBack(scip, pr->visitednodes, node);
   pr->nodeindices_isPath[pr->nodes_index[node]] = FALSE;
}


/** adds new path head edge to failed node */
static inline
void addPathHeadEdge(
   SCIP*                 scip,               /**< SCIP */
   const GRAPH*          g,
   PR*                   pr                  /**< path replacement data structure */
   )
{
   const DCSR* const dcsr = g->dcsr_storage;
   const RANGE* const dcsr_range = dcsr->range;
   const int* const dcsr_heads = dcsr->head;
   const int pathhead = pathGetHead(g, pr);
   const int extnode = pr->failneighbor;
   int j;

   assert(pr->nodes_index[extnode] >= 0);
   assert(!pr->nodeindices_isPath[pr->nodes_index[extnode]]);
   assert(extnode >= 0);

   SCIPdebugMessage("adding new path edge to node %d \n", extnode);

   for( j = dcsr_range[pathhead].start; j != dcsr_range[pathhead].end; j++ )
   {
      if( dcsr_heads[j] == extnode )
         break;
   }
   assert(j != dcsr_range[pathhead].end);
   assert(EQ(dcsr->cost[j], g->cost[dcsr->edgeid[j]]));

   StpVecPushBack(scip, pr->pathedges, dcsr->edgeid[j]);
   pr->pathcost += dcsr->cost[j];
   pr->nodeindices_isPath[pr->nodes_index[extnode]] = TRUE;

   assert(pathGetHead(g, pr) == extnode);
}


/** adds first two path nodes */
static inline
void addInitialPathNodes(
   SCIP*                 scip,               /**< SCIP */
   const GRAPH*          g,
   int                   startedge_tail,
   int                   startdege_head,
   PR*                   pr                  /**< path replacement data structure */
   )
{
   const DCSR* const dcsr = g->dcsr_storage;
   const RANGE* const dcsr_range = dcsr->range;
   const int* const dcsr_heads = dcsr->head;
   SCIP_Real* RESTRICT sp_dists = pr->sp_dists;
   int* RESTRICT sp_starts = pr->sp_starts;
   int starts_final = pr->nfirstneighbors;

   /* add tail node */
   addPathNode(scip, startedge_tail, pr);
   sp_starts[starts_final + 1] = sp_starts[starts_final] + pr->nfirstneighbors;
   for( int i = sp_starts[starts_final], j = 0; i != sp_starts[starts_final + 1]; i++, j++ )
      sp_dists[i] = pr->firstneighborcosts[j];

   /* add head node*/
   addPathNode(scip, startdege_head, pr);
   starts_final++;
   sp_starts[starts_final + 1] = sp_starts[starts_final] + pr->nfirstneighbors;
   for( int i = sp_starts[starts_final]; i != sp_starts[starts_final + 1]; i++ )
      sp_dists[i] = FARAWAY;

   /* update distances by using single edges */
   for( int j = dcsr_range[startdege_head].start; j != dcsr_range[startdege_head].end; j++ )
   {
      const int head = dcsr_heads[j];
      const int head_index = pr->nodes_index[head];

      /* unvisited or path tail? */
      if( head_index < 0 || head == startedge_tail )
         continue;

      assert(0 <= head_index && head_index < pr->nfirstneighbors);
      assert(EQ(sp_dists[sp_starts[starts_final] + head_index], FARAWAY));
      SCIPdebugMessage("setting distance from first path node(%d) for initial neighbor (idx=%d) to %f \n",
            startdege_head, head_index, dcsr->cost[j]);
      sp_dists[sp_starts[starts_final] + head_index] = dcsr->cost[j];
   }
}


/** collects neighbors */
static inline
void pathneighborsCollect(
   SCIP*                 scip,               /**< SCIP */
   const GRAPH*          g,                  /**< graph data structure */
   PR*                   pr                  /**< path replacement data structure */
   )
{
   const DCSR* const dcsr = g->dcsr_storage;
   const RANGE* const dcsr_range = dcsr->range;
   SCIP_Real* RESTRICT sp_dists = pr->sp_dists;
   const int* const dcsr_heads = dcsr->head;
   int* RESTRICT nodes_index = pr->nodes_index;
   int* RESTRICT sp_starts = pr->sp_starts;
   const int basenode = pathGetHead(g, pr);
   const int nfirstneighbors = pr->nfirstneighbors;

   SCIPdebugMessage("extending path to node %d \n", basenode);

   StpVecClear(pr->currneighbors);
   //StpVecClear(pr->currneighborcosts);

   for( int i = dcsr_range[basenode].start; i != dcsr_range[basenode].end; i++ )
   {
      const int head = dcsr_heads[i];
      int head_index = nodes_index[head];

      if( head_index >= 0 && pr->nodeindices_isPath[head_index] )
         continue;

      assert(head_index < StpVecGetSize(pr->visitednodes));
      SCIPdebugMessage("adding neighbor %d  head_index=%d\n", head, head_index);

      StpVecPushBack(scip, pr->currneighbors, head);
      //StpVecPushBack(scip, pr->currneighborcosts, dcsr_costs[i]);

      if( head_index == VNODES_UNSET )
      {
         head_index = StpVecGetSize(pr->visitednodes);
         SCIPdebugMessage("mapping new neighbor %d->%d \n", head, head_index);
         nodes_index[head] = head_index;
         StpVecPushBack(scip, pr->visitednodes, head);
         pr->nodeindices_isPath[head_index] = FALSE;
         assert(head_index + 1 < SP_MAXNSTARTS);

         sp_starts[head_index + 1] = sp_starts[head_index] + nfirstneighbors;

         for( int j = sp_starts[head_index]; j != sp_starts[head_index + 1]; j++ )
            sp_dists[j] = FARAWAY;
      }
   }
}


/** updates distances from neighbors */
static inline
void pathneighborsUpdateDistances(
   SCIP*                 scip,               /**< SCIP */
   const GRAPH*          g,                  /**< graph data structure */
   PR*                   pr                  /**< path replacement data structure */
   )
{
   STP_Vectype(int) currneighbors;
   const DCSR* const dcsr = g->dcsr_storage;
   const RANGE* const dcsr_range = dcsr->range;
   const SCIP_Real* const dcsr_costs = dcsr->cost;
   SCIP_Real* RESTRICT sp_dists = pr->sp_dists;
   const int* const dcsr_heads = dcsr->head;
   const int* const nodes_index = pr->nodes_index;
   const int* const sp_starts = pr->sp_starts;
   const int nfirstneighbors = pr->nfirstneighbors;
   const int nneighbors = StpVecGetSize(pr->currneighbors);
   const int pathtail = pr->pathtail;
   const int pathhead = pathGetHead(g, pr);
   const int nloops = MIN(SP_MAXNPULLS, nneighbors);

   /* NOTE: to keep the following loop easy */
   StpVecPushBack(scip, pr->currneighbors, pathhead);
   currneighbors = pr->currneighbors;

   for( int loop = 0; loop < nloops; loop++ )
   {
      SCIP_Bool hasUpdates = FALSE;
      /* compute distances to each of the initial neighbors */
      for( int iter = 0; iter <= nneighbors; iter++ )
      {
         const int node = currneighbors[iter];
         const int node_start = sp_starts[nodes_index[node]];

         assert(nodes_index[node] >= 0);

         for( int i = dcsr_range[node].start; i != dcsr_range[node].end; i++ )
         {
            const int head = dcsr_heads[i];

            if( nodes_index[head] != VNODES_UNSET )
            {
               const int head_start = sp_starts[nodes_index[head]];
               // todo get prize of head here!

               if( head == pathtail && node == pathhead )
                  continue;

               for( int k = 0; k < nfirstneighbors; k++ )
               {
                  const SCIP_Real newdist = dcsr_costs[i] + sp_dists[head_start + k];

                  if( LT(newdist, sp_dists[node_start + k]) )
                  {
                     SCIPdebugMessage("(l=%d) updating distance for node %d to orgindex %d to %f \n",
                           loop, node, k, newdist);
                     sp_dists[node_start + k] = newdist;
                     hasUpdates = TRUE;
                  }
               }
            }
         }
      }

      if( !hasUpdates )
         break;
   }

   StpVecPopBack(currneighbors);
}


/** preprocess for doing path extension later on */
static inline
void pathExendPrepare(
   SCIP*                 scip,               /**< SCIP */
   const GRAPH*          g,                  /**< graph data structure */
   int                   startedge,          /**< first edge */
   PR*                   pr                  /**< path replacement data structure */
   )
{
   const int basetail = g->tail[startedge];
   const int basehead = g->head[startedge];
   const DCSR* const dcsr = g->dcsr_storage;
   const RANGE* const dcsr_range = dcsr->range;
   const int* const dcsr_heads = dcsr->head;
   const SCIP_Real* const dcsr_costs = dcsr->cost;
   SCIP_Real* RESTRICT sp_dists = pr->sp_dists;
   int* RESTRICT sp_starts = pr->sp_starts;

   assert(0 == StpVecGetSize(pr->pathedges));
   assert(0 == StpVecGetSize(pr->visitednodes));
   assert(0 == StpVecGetSize(pr->currneighbors));
   assert(0 == StpVecGetSize(pr->firstneighborcosts));

   SCIPdebugMessage("---Checking edge %d->%d \n\n", basetail, basehead);

   pr->pathcost = g->cost[startedge];
   pr->pathtail = basetail;
   StpVecPushBack(scip, pr->pathedges, startedge);

   for( int i = dcsr_range[basetail].start; i != dcsr_range[basetail].end; i++ )
   {
      const int head = dcsr_heads[i];
      assert(pr->nodes_index[head] == VNODES_UNSET);

      if( head == basehead )
         continue;

      SCIPdebugMessage("mapping first neighbor %d->%d \n", head, StpVecGetSize(pr->visitednodes));

      addNonPathNode(scip, head, pr);
      StpVecPushBack(scip, pr->currneighbors, head);
      StpVecPushBack(scip, pr->firstneighborcosts, dcsr_costs[i]);
   }

   pr->nfirstneighbors = StpVecGetSize(pr->currneighbors);
   SCIPdebugMessage("Having %d initial neighbors \n", pr->nfirstneighbors);

   sp_starts[0] = 0;
   for( int i = 0; i < pr->nfirstneighbors; i++ )
   {
      const int neighbor = pr->currneighbors[i];
      const SCIP_Real basecost = pr->firstneighborcosts[i];
      sp_starts[i + 1] = sp_starts[i] + pr->nfirstneighbors;

      /* set 2-edge distances via path tail */
      for( int j = sp_starts[i], k = 0; j != sp_starts[i + 1]; j++, k++ )
         sp_dists[j] = basecost + pr->firstneighborcosts[k];

#ifdef SCIP_DEBUG
      for( int j = sp_starts[i]; j != sp_starts[i + 1]; j++ )
         printf("%d->%d dist=%f \n", i, j - sp_starts[i], sp_dists[j]);
#endif

      assert(EQ(sp_dists[sp_starts[i] + i], 2.0 * basecost));
      /* set self-distance */
      sp_dists[sp_starts[i] + i] = 0.0;

      /* update distances by using single edges */
      for( int j = dcsr_range[neighbor].start; j != dcsr_range[neighbor].end; j++ )
      {
         const int head = dcsr_heads[j];
         const int head_index = pr->nodes_index[head];

         /* unvisited or path tail? */
         if( head_index < 0 || head == basetail )
            continue;

         assert(0 <= head_index && head_index < pr->nfirstneighbors);

#ifdef SCIP_DEBUG
         if( LT(dcsr_costs[j], sp_dists[sp_starts[i] + head_index]) )
            printf("updating %d->%d distold=%f distnew=%f \n", i, head_index, sp_dists[sp_starts[i] + head_index], dcsr_costs[j]);
#endif
         if( LT(dcsr_costs[j], sp_dists[sp_starts[i] + head_index]) )
            sp_dists[sp_starts[i] + head_index] = dcsr_costs[j];
      }
   }

   addInitialPathNodes(scip, g, basetail, basehead, pr);
}


/** tries to eliminate edges of path starting from edge */
static inline
void pathExend(
   SCIP*                 scip,               /**< SCIP */
   const GRAPH*          g,                  /**< graph data structure */
   PR*                   pr,                 /**< path replacement data structure */
   SCIP_Bool*            isExendible,
   SCIP_Bool*            isRedundant
   )
{
   const int npathedges = StpVecGetSize(pr->pathedges);
   const int pathhead = pathGetHead(g, pr);
   SCIP_Bool needCombRuleOut = FALSE;
   SCIP_Bool isSingleRuledOut = FALSE;
   SCIP_Bool isCombRuledOut = FALSE;

   assert(*isExendible);
   assert(!(*isRedundant));

   if( npathedges >= SP_MAXLENGTH || g->grad[pathhead] >= SP_MAXDEGREE || Is_term(g->term[pathhead]) )
   {
      *isExendible = FALSE;
      return;
   }

   pathneighborsCollect(scip, g, pr);
   pathneighborsUpdateDistances(scip, g, pr);

   if( StpVecGetSize(pr->currneighbors) == 0 )
   {
      // todo really true?
      *isRedundant = TRUE;
      return;
   }

   ruleOutFromHead(pr, &needCombRuleOut);

   /* NOTE: if we have exactly one neighbor, and a failed neighbor, they are the same and we have to extend */
   if( StpVecGetSize(pr->currneighbors) == 1 && pr->failneighbor != VNODES_UNSET )
   {
      addPathHeadEdge(scip, g, pr);
      return;
   }

   /* we try combination rule-out anyway! */
   ruleOutFromTailCombs(g, pr, &isCombRuledOut);

   if( needCombRuleOut && !isCombRuledOut )
   {
      *isExendible = FALSE;
      return;
   }

   ruleOutFromTailSingle(g, pr, &isSingleRuledOut);

   if( !isSingleRuledOut )
   {
      *isExendible = FALSE;
      return;
   }

   if( isCombRuledOut )
   {
      *isRedundant = TRUE;
      return;
   }

   if( pr->failneighbor == VNODES_UNSET )
   {
      *isRedundant = TRUE;
      return;
   }

   /* if neither of the above cases holds, we extend along the failed neighbor */
   addPathHeadEdge(scip, g, pr);
}


/** initializes */
static
SCIP_RETCODE prInit(
   SCIP*                 scip,               /**< SCIP data structure */
   const GRAPH*          g,                  /**< graph data structure */
   PR**                  pathreplace         /**< to initialize */
   )
{
   PR* pr;
   const int nnodes = graph_get_nNodes(g);

   SCIP_CALL( SCIPallocMemory(scip, pathreplace) );
   pr = *pathreplace;

   SCIP_CALL( SCIPallocMemoryArray(scip, &pr->nodeindices_isPath, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &pr->nodes_index, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &pr->sp_starts, SP_MAXNSTARTS) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &pr->sp_dists, SP_MAXNDISTS) );

   for( int i = 0; i < nnodes; i++ )
      pr->nodes_index[i] = VNODES_UNSET;

   pr->currneighbors = NULL;
   //pr->currneighborcosts = NULL;
   pr->firstneighborcosts = NULL;
   pr->pathedges = NULL;
   pr->visitednodes = NULL;
   pr->nfirstneighbors = -1;
   pr->nnodes = nnodes;
   StpVecReserve(scip, pr->currneighbors, SP_MAXDEGREE);
   StpVecReserve(scip, pr->firstneighborcosts, SP_MAXDEGREE);
  // StpVecReserve(scip, pr->currneighborcosts, SP_MAXDEGREE);
   StpVecReserve(scip, pr->pathedges, SP_MAXLENGTH);
   StpVecReserve(scip, pr->visitednodes, SP_MAXLENGTH);

 //  SCIP_CALL( graph_heap_create(scip, nnodes, NULL, NULL, &pr->dheap) );

   return SCIP_OKAY;
}


/** cleans temporary data */
static
void prClean(
   PR*                   pr                 /**< to be cleaned */
   )
{
   const int nvisited = StpVecGetSize(pr->visitednodes);

   for( int i = 0; i < nvisited; i++ )
   {
      const int node = pr->visitednodes[i];
      assert(node >= 0 && node < pr->nnodes && pr->nodes_index[node] != VNODES_UNSET);
      pr->nodes_index[node] = VNODES_UNSET;
   }

   StpVecClear(pr->firstneighborcosts);
   //StpVecClear(pr->currneighborcosts);
   StpVecClear(pr->currneighbors);
   StpVecClear(pr->pathedges);
   StpVecClear(pr->visitednodes);

#ifndef NDEBUG
   for( int i = 0; i < pr->nnodes; i++ )
      assert(pr->nodes_index[i] == VNODES_UNSET);

   for( int i = 0; i < SP_MAXNSTARTS; i++ )
      pr->sp_starts[i] = -1;
#endif
}


/** frees */
static
void prFree(
   SCIP*                 scip,               /**< SCIP data structure */
   PR**                  pathreplace         /**< to be freed */
   )
{
   PR* pr = *pathreplace;

   //graph_heap_free(scip, FALSE, FALSE, &pr->dheap);
   StpVecFree(scip, pr->pathedges);
   StpVecFree(scip, pr->currneighbors);
   StpVecFree(scip, pr->visitednodes);
  // StpVecFree(scip, pr->currneighborcosts);
   StpVecFree(scip, pr->firstneighborcosts);
   SCIPfreeMemoryArray(scip, &pr->sp_dists);
   SCIPfreeMemoryArray(scip, &pr->sp_starts);
   SCIPfreeMemoryArray(scip, &pr->nodes_index);
   SCIPfreeMemoryArray(scip, &pr->nodeindices_isPath);

   SCIPfreeMemory(scip, pathreplace);
}


/** is execution of path replacement reduction method promising? */
static
SCIP_Bool prIsPromising(
   const GRAPH*          g                   /**< graph structure */
   )
{
   // todo
   return TRUE;
}


/** tries to eliminate edges of path starting from edge */
static inline
SCIP_RETCODE processPath(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   startedge,          /**< edge to start from (head) */
   PR*                   pathreplace,        /**< path replacement data structure */
   GRAPH*                g,                  /**< graph data structure */
   int*                  nelims              /**< pointer to number of reductions */
   )
{
   SCIP_Bool pathIsExtendable = TRUE;
   SCIP_Bool pathIsRedundant = FALSE;
   const int tail = g->tail[startedge];
   const int head = g->head[startedge];
   assert(StpVecGetSize(pathreplace->pathedges) == 0);

   if( g->grad[tail] >= SP_MAXDEGREE || g->grad[head] >= SP_MAXDEGREE || Is_term(g->term[tail]) || Is_term(g->term[head]) )
      return SCIP_OKAY;

   if( g->grad[tail] <= 1 )
      return SCIP_OKAY;

   pathExendPrepare(scip, g, startedge, pathreplace);

   while( pathIsExtendable )
   {
      assert(!pathIsRedundant);
      pathExend(scip, g, pathreplace, &pathIsExtendable, &pathIsRedundant);

      if( pathIsRedundant )
      {
         SCIPdebugMessage("deleting edge %d-%d \n", g->tail[startedge], g->head[startedge]);
         graph_edge_delFull(scip, g, startedge, TRUE);
         (*nelims)++;
         break;
      }
   }

   prClean(pathreplace);

   return SCIP_OKAY;
}


/** executes reduction method */
static
SCIP_RETCODE pathreplaceExec(
   SCIP*                 scip,               /**< SCIP data structure */
   PR*                   pathreplace,        /**< path replacement data structure */
   GRAPH*                g,                  /**< graph data structure */
   int*                  nelims              /**< pointer to number of reductions */
   )
{
   const int nedges = graph_get_nEdges(g);
   // todo have edge stack?

   for( int e = 0; e < nedges; e++ )
   {
      if( g->oeat[e] == EAT_FREE )
         continue;

      SCIP_CALL( processPath(scip, e, pathreplace, g, nelims) );

      // todo if edgestack: check whether edge was killed, otherwise go different!
      // afterwards deactivate edge!
   }


   return SCIP_OKAY;
}

/*
 * Interface methods
 */


/** paths elimination */
SCIP_RETCODE reduce_pathreplace(
   SCIP*                 scip,               /**< SCIP data structure */
   GRAPH*                g,                  /**< graph data structure */
   int*                  nelims              /**< pointer to number of reductions */
   )
{
   PR* pathreplace;

   if( !prIsPromising(g) )
   {
      return SCIP_OKAY;
   }

   SCIP_CALL( graph_init_dcsr(scip, g) );
   SCIP_CALL( prInit(scip, g, &pathreplace) );

   SCIP_CALL( pathreplaceExec(scip, pathreplace, g, nelims) );

   prFree(scip, &pathreplace);
   graph_free_dcsr(scip, g);

   assert(graph_valid(scip, g));

   return SCIP_OKAY;
}

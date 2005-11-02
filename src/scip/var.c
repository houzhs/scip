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
#pragma ident "@(#) $Id: var.c,v 1.194 2005/11/02 11:14:44 bzfpfend Exp $"

/**@file   var.c
 * @brief  methods for problem variables
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "scip/def.h"
#include "scip/message.h"
#include "scip/set.h"
#include "scip/stat.h"
#include "scip/history.h"
#include "scip/event.h"
#include "scip/lp.h"
#include "scip/var.h"
#include "scip/implics.h"
#include "scip/prob.h"
#include "scip/primal.h"
#include "scip/cons.h"
#include "scip/prop.h"
#include "scip/debug.h"




/*
 * hole, holelist, and domain methods
 */

/** creates a new holelist element */
static
SCIP_RETCODE holelistCreate(
   SCIP_HOLELIST**       holelist,           /**< pointer to holelist to create */
   BMS_BLKMEM*           blkmem,             /**< block memory for target holelist */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             left,               /**< left bound of open interval in new hole */
   SCIP_Real             right               /**< right bound of open interval in new hole */
   )
{
   assert(holelist != NULL);
   assert(blkmem != NULL);
   assert(SCIPsetIsLT(set, left, right));

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, holelist) );
   (*holelist)->hole.left = left;
   (*holelist)->hole.right = right;
   (*holelist)->next = NULL;

   return SCIP_OKAY;
}

/** frees all elements in the holelist */
static
void holelistFree(
   SCIP_HOLELIST**       holelist,           /**< pointer to holelist to free */
   BMS_BLKMEM*           blkmem              /**< block memory for target holelist */
   )
{
   assert(holelist != NULL);
   assert(blkmem != NULL);

   while( *holelist != NULL )
   {
      SCIP_HOLELIST* next;

      next = (*holelist)->next;
      BMSfreeBlockMemory(blkmem, holelist);
      *holelist = next;
   }
}

/** duplicates a list of holes */
static
SCIP_RETCODE holelistDuplicate(
   SCIP_HOLELIST**       target,             /**< pointer to target holelist */
   BMS_BLKMEM*           blkmem,             /**< block memory for target holelist */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_HOLELIST*        source              /**< holelist to duplicate */
   )
{
   assert(target != NULL);

   while( source != NULL )
   {
      assert(source->next == NULL || SCIPsetIsGE(set, source->next->hole.left, source->hole.right));
      SCIP_CALL( holelistCreate(target, blkmem, set, source->hole.left, source->hole.right) );
      source = source->next;
      target = &(*target)->next;
   }

   return SCIP_OKAY;
}

/** adds a hole to the domain */
static
SCIP_RETCODE domAddHole(
   SCIP_DOM*             dom,                /**< domain to add hole to */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             left,               /**< left bound of open interval in new hole */
   SCIP_Real             right               /**< right bound of open interval in new hole */
   )
{
   SCIP_HOLELIST** insertpos;
   SCIP_HOLELIST* nextholelist;

   assert(dom != NULL);

   /* sort new hole in holelist of variable */
   insertpos = &dom->holelist;
   while( *insertpos != NULL && (*insertpos)->hole.left < left )
      insertpos = &(*insertpos)->next;

   nextholelist = *insertpos;

   SCIP_CALL( holelistCreate(insertpos, blkmem, set, left, right) );
   (*insertpos)->next = nextholelist;
   
   return SCIP_OKAY;
}

#if 0 /* for future use */
/** merges overlapping holes into single holes, moves bounds respectively */
static
SCIP_RETCODE domMerge(
   SCIP_DOM*             dom,                /**< domain to merge */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   SCIP_HOLELIST** holelistptr;
   SCIP_Real* lastrightptr;

   assert(dom != NULL);
   assert(SCIPsetIsLE(set, dom->lb, dom->ub));

   lastrightptr = &dom->lb;  /* lower bound is the right bound of the hole (-infinity,lb) */
   holelistptr = &dom->holelist;

   while( *holelistptr != NULL )
   {
      assert(SCIPsetIsLT(set, (*holelistptr)->hole.left, (*holelistptr)->hole.right));

      if( SCIPsetIsGE(set, (*holelistptr)->hole.left, dom->ub) )
      {
         /* the remaining holes start behind the upper bound: kill them */
         holelistFree(holelistptr, blkmem);
         assert(*holelistptr == NULL);
      }
      else if( SCIPsetIsGT(set, (*holelistptr)->hole.right, dom->ub) )
      {
         /* the hole overlaps the upper bound: decrease upper bound, kill this and all remaining holes */
         dom->ub = (*holelistptr)->hole.left;
         holelistFree(holelistptr, blkmem);
         assert(*holelistptr == NULL);
      }
      else if( SCIPsetIsGT(set, *lastrightptr, (*holelistptr)->hole.left) )
      {
         /* the right bound of the last hole is greater than the left bound of this hole:
          * increase the right bound of the last hole, delete this hole
          */
         SCIP_HOLELIST* next;

         *lastrightptr = MAX(*lastrightptr, (*holelistptr)->hole.right);
         next = (*holelistptr)->next;
         (*holelistptr)->next = NULL;
         holelistFree(holelistptr, blkmem);
         *holelistptr = next;
      }
      else
      {
         /* the holes do not overlap: update lastrightptr and holelistptr to the next hole */
         lastrightptr = &(*holelistptr)->hole.right;
         holelistptr = &(*holelistptr)->next;
      }
   }

   return SCIP_OKAY;
}
#endif




/*
 * domain change methods
 */

/** ensures, that bound change info array for lower bound changes can store at least num entries */
static
SCIP_RETCODE varEnsureLbchginfosSize(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(var != NULL);
   assert(var->nlbchginfos <= var->lbchginfossize);
   assert(SCIPvarIsTransformed(var));

   if( num > var->lbchginfossize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &var->lbchginfos, var->lbchginfossize, newsize) );
      var->lbchginfossize = newsize;
   }
   assert(num <= var->lbchginfossize);

   return SCIP_OKAY;
}

/** ensures, that bound change info array for upper bound changes can store at least num entries */
static
SCIP_RETCODE varEnsureUbchginfosSize(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(var != NULL);
   assert(var->nubchginfos <= var->ubchginfossize);
   assert(SCIPvarIsTransformed(var));

   if( num > var->ubchginfossize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &var->ubchginfos, var->ubchginfossize, newsize) );
      var->ubchginfossize = newsize;
   }
   assert(num <= var->ubchginfossize);

   return SCIP_OKAY;
}

/** adds domain change info to the variable's lower bound change info array */
static
SCIP_RETCODE varAddLbchginfo(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             oldbound,           /**< old value for bound */
   SCIP_Real             newbound,           /**< new value for bound */
   int                   depth,              /**< depth in the tree, where the bound change takes place */
   int                   pos,                /**< position of the bound change in its bound change array */
   SCIP_VAR*             infervar,           /**< variable that was changed (parent of var, or var itself) */
   SCIP_CONS*            infercons,          /**< constraint that infered this bound change, or NULL */
   SCIP_PROP*            inferprop,          /**< propagator that deduced the bound change, or NULL */
   int                   inferinfo,          /**< user information for inference to help resolving the conflict */
   SCIP_BOUNDTYPE        inferboundtype,     /**< type of bound for inference var: lower or upper bound */
   SCIP_BOUNDCHGTYPE     boundchgtype        /**< bound change type: branching decision or infered bound change */
   )
{
   assert(var != NULL);
   assert(SCIPsetIsLT(set, oldbound, newbound));
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS || SCIPsetIsFeasIntegral(set, oldbound));
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS || SCIPsetIsFeasIntegral(set, newbound));
   assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY || SCIPsetIsEQ(set, oldbound, 0.0));
   assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY || SCIPsetIsEQ(set, newbound, 1.0));
   assert(boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING || infervar != NULL);
   assert((boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER) == (infercons != NULL));
   assert(boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER || inferprop == NULL);

   SCIPdebugMessage("adding lower bound change info to var <%s>[%g,%g]: depth=%d, pos=%d, %g -> %g\n",
      SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, depth, pos, oldbound, newbound);

   SCIP_CALL( varEnsureLbchginfosSize(var, blkmem, set, var->nlbchginfos+1) );
   var->lbchginfos[var->nlbchginfos].oldbound = oldbound;
   var->lbchginfos[var->nlbchginfos].newbound = newbound;
   var->lbchginfos[var->nlbchginfos].var = var;
   var->lbchginfos[var->nlbchginfos].bdchgidx.depth = depth;
   var->lbchginfos[var->nlbchginfos].bdchgidx.pos = pos;
   var->lbchginfos[var->nlbchginfos].boundchgtype = boundchgtype; /*lint !e641*/
   var->lbchginfos[var->nlbchginfos].boundtype = SCIP_BOUNDTYPE_LOWER; /*lint !e641*/
   var->lbchginfos[var->nlbchginfos].inferboundtype = inferboundtype; /*lint !e641*/
   var->lbchginfos[var->nlbchginfos].inferencedata.var = infervar;
   var->lbchginfos[var->nlbchginfos].inferencedata.info = inferinfo;

   switch( boundchgtype )
   {
   case SCIP_BOUNDCHGTYPE_BRANCHING:
      break;
   case SCIP_BOUNDCHGTYPE_CONSINFER:
      assert(infercons != NULL);
      var->lbchginfos[var->nlbchginfos].inferencedata.reason.cons = infercons;
      break;
   case SCIP_BOUNDCHGTYPE_PROPINFER:
      var->lbchginfos[var->nlbchginfos].inferencedata.reason.prop = inferprop;
      break;
   default:
      SCIPerrorMessage("invalid bound change type %d\n", boundchgtype);
      return SCIP_INVALIDDATA;
   }

   var->nlbchginfos++;

   assert(var->nlbchginfos < 2
      || SCIPbdchgidxIsEarlier(&var->lbchginfos[var->nlbchginfos-2].bdchgidx,
         &var->lbchginfos[var->nlbchginfos-1].bdchgidx));

   return SCIP_OKAY;
}

/** adds domain change info to the variable's upper bound change info array */
static
SCIP_RETCODE varAddUbchginfo(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             oldbound,           /**< old value for bound */
   SCIP_Real             newbound,           /**< new value for bound */
   int                   depth,              /**< depth in the tree, where the bound change takes place */
   int                   pos,                /**< position of the bound change in its bound change array */
   SCIP_VAR*             infervar,           /**< variable that was changed (parent of var, or var itself) */
   SCIP_CONS*            infercons,          /**< constraint that infered this bound change, or NULL */
   SCIP_PROP*            inferprop,          /**< propagator that deduced the bound change, or NULL */
   int                   inferinfo,          /**< user information for inference to help resolving the conflict */
   SCIP_BOUNDTYPE        inferboundtype,     /**< type of bound for inference var: lower or upper bound */
   SCIP_BOUNDCHGTYPE     boundchgtype        /**< bound change type: branching decision or infered bound change */
   )
{
   assert(var != NULL);
   assert(SCIPsetIsGT(set, oldbound, newbound));
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS || SCIPsetIsFeasIntegral(set, oldbound));
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS || SCIPsetIsFeasIntegral(set, newbound));
   assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY || SCIPsetIsEQ(set, oldbound, 1.0));
   assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY || SCIPsetIsEQ(set, newbound, 0.0));
   assert(boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING || infervar != NULL);
   assert((boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER) == (infercons != NULL));
   assert(boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER || inferprop == NULL);

   SCIPdebugMessage("adding upper bound change info to var <%s>[%g,%g]: depth=%d, pos=%d, %g -> %g\n",
      SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, depth, pos, oldbound, newbound);

   SCIP_CALL( varEnsureUbchginfosSize(var, blkmem, set, var->nubchginfos+1) );
   var->ubchginfos[var->nubchginfos].oldbound = oldbound;
   var->ubchginfos[var->nubchginfos].newbound = newbound;
   var->ubchginfos[var->nubchginfos].var = var;
   var->ubchginfos[var->nubchginfos].bdchgidx.depth = depth;
   var->ubchginfos[var->nubchginfos].bdchgidx.pos = pos;
   var->ubchginfos[var->nubchginfos].boundchgtype = boundchgtype; /*lint !e641*/
   var->ubchginfos[var->nubchginfos].boundtype = SCIP_BOUNDTYPE_UPPER; /*lint !e641*/
   var->ubchginfos[var->nubchginfos].inferboundtype = inferboundtype; /*lint !e641*/
   var->ubchginfos[var->nubchginfos].inferencedata.var = infervar;
   var->ubchginfos[var->nubchginfos].inferencedata.info = inferinfo;

   switch( boundchgtype )
   {
   case SCIP_BOUNDCHGTYPE_BRANCHING:
      break;
   case SCIP_BOUNDCHGTYPE_CONSINFER:
      assert(infercons != NULL);
      var->ubchginfos[var->nubchginfos].inferencedata.reason.cons = infercons;
      break;
   case SCIP_BOUNDCHGTYPE_PROPINFER:
      var->ubchginfos[var->nubchginfos].inferencedata.reason.prop = inferprop;
      break;
   default:
      SCIPerrorMessage("invalid bound change type %d\n", boundchgtype);
      return SCIP_INVALIDDATA;
   }

   var->nubchginfos++;

   assert(var->nubchginfos < 2
      || SCIPbdchgidxIsEarlier(&var->ubchginfos[var->nubchginfos-2].bdchgidx,
         &var->ubchginfos[var->nubchginfos-1].bdchgidx));

   return SCIP_OKAY;
}

/** applies single bound change */
SCIP_RETCODE SCIPboundchgApply(
   SCIP_BOUNDCHG*        boundchg,           /**< bound change to apply */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   int                   depth,              /**< depth in the tree, where the bound change takes place */
   int                   pos,                /**< position of the bound change in its bound change array */
   SCIP_Bool*            cutoff              /**< pointer to store whether an infeasible bound change was detected */
   )
{
   SCIP_VAR* var;

   assert(boundchg != NULL);
   assert(stat != NULL);
   assert(depth >= 0);
   assert(pos >= 0);
   assert(cutoff != NULL);

   *cutoff = FALSE;

   var = boundchg->var;
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);

   /* apply bound change */
   switch( boundchg->boundtype )
   {
   case SCIP_BOUNDTYPE_LOWER:
      /* check, if the bound change is still active (could be replaced by inference due to repropagation of higher node) */
      if( SCIPsetIsGT(set, boundchg->newbound, var->locdom.lb) )
      {
         if( SCIPsetIsLE(set, boundchg->newbound, var->locdom.ub) )
         {
            /* add the bound change info to the variable's bound change info array */
            switch( boundchg->boundchgtype )
            {
            case SCIP_BOUNDCHGTYPE_BRANCHING:
               SCIPdebugMessage(" -> branching: new lower bound of <%s>[%g,%g]: %g\n",
                  SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
               SCIP_CALL( varAddLbchginfo(var, blkmem, set, var->locdom.lb, boundchg->newbound, depth, pos,
                     NULL, NULL, NULL, 0, SCIP_BOUNDTYPE_LOWER, SCIP_BOUNDCHGTYPE_BRANCHING) );
               stat->lastbranchvar = var;
               stat->lastbranchdir = SCIP_BRANCHDIR_UPWARDS;
               break;

            case SCIP_BOUNDCHGTYPE_CONSINFER:
               assert(boundchg->data.inferencedata.reason.cons != NULL);
               SCIPdebugMessage(" -> constraint <%s> inference: new lower bound of <%s>[%g,%g]: %g\n",
                  SCIPconsGetName(boundchg->data.inferencedata.reason.cons),
                  SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
               SCIP_CALL( varAddLbchginfo(var, blkmem, set, var->locdom.lb, boundchg->newbound, depth, pos,
                     boundchg->data.inferencedata.var, boundchg->data.inferencedata.reason.cons, NULL,
                     boundchg->data.inferencedata.info,
                     (SCIP_BOUNDTYPE)(boundchg->inferboundtype), SCIP_BOUNDCHGTYPE_CONSINFER) );
               break;

            case SCIP_BOUNDCHGTYPE_PROPINFER:
               SCIPdebugMessage(" -> propagator <%s> inference: new lower bound of <%s>[%g,%g]: %g\n",
                  boundchg->data.inferencedata.reason.prop != NULL
                  ? SCIPpropGetName(boundchg->data.inferencedata.reason.prop) : "-",
                  SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
               SCIP_CALL( varAddLbchginfo(var, blkmem, set, var->locdom.lb, boundchg->newbound, depth, pos,
                     boundchg->data.inferencedata.var, NULL, boundchg->data.inferencedata.reason.prop,
                     boundchg->data.inferencedata.info,
                     (SCIP_BOUNDTYPE)(boundchg->inferboundtype), SCIP_BOUNDCHGTYPE_PROPINFER) );
               break;

            default:
               SCIPerrorMessage("invalid bound change type %d\n", boundchg->boundchgtype);
               return SCIP_INVALIDDATA;
            }
            
            /* change local bound of variable */
            SCIP_CALL( SCIPvarChgLbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, boundchg->newbound) );
         }
         else
         {
            SCIPdebugMessage(" -> cutoff: new lower bound of <%s>[%g,%g]: %g\n",
               SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
            boundchg->var = NULL;
            *cutoff = TRUE;
         }
      }
      else
      {
         /* mark bound change to be inactive */
         SCIPdebugMessage(" -> inactive %s: new lower bound of <%s>[%g,%g]: %g\n",
            (SCIP_BOUNDCHGTYPE)boundchg->boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING ? "branching" : "inference",
            SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
         boundchg->var = NULL;
      }
      break;

   case SCIP_BOUNDTYPE_UPPER:
      /* check, if the bound change is still active (could be replaced by inference due to repropagation of higher node) */
      if( SCIPsetIsLT(set, boundchg->newbound, var->locdom.ub) )
      {
         if( SCIPsetIsGE(set, boundchg->newbound, var->locdom.lb) )
         {
            /* add the bound change info to the variable's bound change info array */
            switch( boundchg->boundchgtype )
            {
            case SCIP_BOUNDCHGTYPE_BRANCHING:
               SCIPdebugMessage(" -> branching: new upper bound of <%s>[%g,%g]: %g\n",
                  SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
               SCIP_CALL( varAddUbchginfo(var, blkmem, set, var->locdom.ub, boundchg->newbound, depth, pos,
                     NULL, NULL, NULL, 0, SCIP_BOUNDTYPE_UPPER, SCIP_BOUNDCHGTYPE_BRANCHING) );
               stat->lastbranchvar = var;
               stat->lastbranchdir = SCIP_BRANCHDIR_DOWNWARDS;
               break;

            case SCIP_BOUNDCHGTYPE_CONSINFER:
               assert(boundchg->data.inferencedata.reason.cons != NULL);
               SCIPdebugMessage(" -> constraint <%s> inference: new upper bound of <%s>[%g,%g]: %g\n",
                  SCIPconsGetName(boundchg->data.inferencedata.reason.cons),
                  SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
               SCIP_CALL( varAddUbchginfo(var, blkmem, set, var->locdom.ub, boundchg->newbound, depth, pos,
                     boundchg->data.inferencedata.var, boundchg->data.inferencedata.reason.cons, NULL,
                     boundchg->data.inferencedata.info,
                     (SCIP_BOUNDTYPE)(boundchg->inferboundtype), SCIP_BOUNDCHGTYPE_CONSINFER) );
               break;

            case SCIP_BOUNDCHGTYPE_PROPINFER:
               SCIPdebugMessage(" -> propagator <%s> inference: new upper bound of <%s>[%g,%g]: %g\n",
                  boundchg->data.inferencedata.reason.prop != NULL
                  ? SCIPpropGetName(boundchg->data.inferencedata.reason.prop) : "-",
                  SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
               SCIP_CALL( varAddUbchginfo(var, blkmem, set, var->locdom.ub, boundchg->newbound, depth, pos,
                     boundchg->data.inferencedata.var, NULL, boundchg->data.inferencedata.reason.prop,
                     boundchg->data.inferencedata.info,
                     (SCIP_BOUNDTYPE)(boundchg->inferboundtype), SCIP_BOUNDCHGTYPE_PROPINFER) );
               break;

            default:
               SCIPerrorMessage("invalid bound change type %d\n", boundchg->boundchgtype);
               return SCIP_INVALIDDATA;
            }

            /* change local bound of variable */
            SCIP_CALL( SCIPvarChgUbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, boundchg->newbound) );
         }
         else
         {
            SCIPdebugMessage(" -> cutoff: new upper bound of <%s>[%g,%g]: %g\n",
               SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
            boundchg->var = NULL;
            *cutoff = TRUE;
         }
      }
      else
      {
         /* mark bound change to be inactive */
         SCIPdebugMessage(" -> inactive %s: new upper bound of <%s>[%g,%g]: %g\n",
            (SCIP_BOUNDCHGTYPE)boundchg->boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING ? "branching" : "inference",
            SCIPvarGetName(var), var->locdom.lb, var->locdom.ub, boundchg->newbound);
         boundchg->var = NULL;
      }
      break;

   default:
      SCIPerrorMessage("unknown bound type\n");
      return SCIP_INVALIDDATA;
   }

   /* update the branching and inference history */
   if( !boundchg->applied && boundchg->var != NULL )
   {
      assert(var == boundchg->var);
      
      if( (SCIP_BOUNDCHGTYPE)boundchg->boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING )
      {
         SCIP_CALL( SCIPvarIncNBranchings(var, stat, depth, 
               (SCIP_BOUNDTYPE)boundchg->boundtype == SCIP_BOUNDTYPE_LOWER
               ? SCIP_BRANCHDIR_UPWARDS : SCIP_BRANCHDIR_DOWNWARDS) );
      }
      else if( stat->lastbranchvar != NULL )
      {
         /**@todo if last branching variable is unknown, retrieve it from the nodes' boundchg arrays */
         SCIP_CALL( SCIPvarIncNInferences(stat->lastbranchvar, stat, stat->lastbranchdir) );
      }
      boundchg->applied = TRUE;
   }

   return SCIP_OKAY;
}

/** undoes single bound change */
SCIP_RETCODE SCIPboundchgUndo(
   SCIP_BOUNDCHG*        boundchg,           /**< bound change to remove */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   )
{
   SCIP_VAR* var;

   assert(boundchg != NULL);
   assert(stat != NULL);

   var = boundchg->var;
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);

   /* undo bound change: apply the previous bound change of variable */
   switch( boundchg->boundtype )
   {
   case SCIP_BOUNDTYPE_LOWER:
      var->nlbchginfos--;
      assert(var->nlbchginfos >= 0);
      assert(var->lbchginfos != NULL);
      assert(var->lbchginfos[var->nlbchginfos].newbound == var->locdom.lb); /*lint !e777*/
      assert(boundchg->newbound == var->locdom.lb); /*lint !e777*/

      SCIPdebugMessage("removed lower bound change info of var <%s>[%g,%g]: depth=%d, pos=%d, %g -> %g\n",
         SCIPvarGetName(var), var->locdom.lb, var->locdom.ub,
         var->lbchginfos[var->nlbchginfos].bdchgidx.depth, var->lbchginfos[var->nlbchginfos].bdchgidx.pos, 
         var->lbchginfos[var->nlbchginfos].oldbound, var->lbchginfos[var->nlbchginfos].newbound);

      /* reinstall the previous local bound */
      SCIP_CALL( SCIPvarChgLbLocal(boundchg->var, blkmem, set, stat, lp, branchcand, eventqueue,
            var->lbchginfos[var->nlbchginfos].oldbound) );
      break;

   case SCIP_BOUNDTYPE_UPPER:
      var->nubchginfos--;
      assert(var->nubchginfos >= 0);
      assert(var->ubchginfos != NULL);
      assert(var->ubchginfos[var->nubchginfos].newbound == var->locdom.ub); /*lint !e777*/
      assert(boundchg->newbound == var->locdom.ub); /*lint !e777*/

      SCIPdebugMessage("removed upper bound change info of var <%s>[%g,%g]: depth=%d, pos=%d, %g -> %g\n",
         SCIPvarGetName(var), var->locdom.lb, var->locdom.ub,
         var->ubchginfos[var->nubchginfos].bdchgidx.depth, var->ubchginfos[var->nubchginfos].bdchgidx.pos, 
         var->ubchginfos[var->nubchginfos].oldbound, var->ubchginfos[var->nubchginfos].newbound);

      /* reinstall the previous local bound */
      SCIP_CALL( SCIPvarChgUbLocal(boundchg->var, blkmem, set, stat, lp, branchcand, eventqueue,
            var->ubchginfos[var->nubchginfos].oldbound) );
      break;

   default:
      SCIPerrorMessage("unknown bound type\n");
      return SCIP_INVALIDDATA;
   }

   /* update last branching variable */
   if( (SCIP_BOUNDCHGTYPE)boundchg->boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING )
      stat->lastbranchvar = NULL;

   return SCIP_OKAY;
}

/** captures branching and inference data of bound change */
static
SCIP_RETCODE boundchgCaptureData(
   SCIP_BOUNDCHG*        boundchg            /**< bound change to remove */
   )
{
   assert(boundchg != NULL);

   switch( boundchg->boundchgtype )
   {
   case SCIP_BOUNDCHGTYPE_BRANCHING:
   case SCIP_BOUNDCHGTYPE_PROPINFER:
      break;

   case SCIP_BOUNDCHGTYPE_CONSINFER:
      assert(boundchg->data.inferencedata.var != NULL);
      assert(boundchg->data.inferencedata.reason.cons != NULL);
      SCIPconsCapture(boundchg->data.inferencedata.reason.cons);
      break;
      
   default:
      SCIPerrorMessage("invalid bound change type\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** releases branching and inference data of bound change */
static
SCIP_RETCODE boundchgReleaseData(
   SCIP_BOUNDCHG*        boundchg,           /**< bound change to remove */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(boundchg != NULL);

   switch( boundchg->boundchgtype )
   {
   case SCIP_BOUNDCHGTYPE_BRANCHING:
   case SCIP_BOUNDCHGTYPE_PROPINFER:
      break;

   case SCIP_BOUNDCHGTYPE_CONSINFER:
      assert(boundchg->data.inferencedata.var != NULL);
      assert(boundchg->data.inferencedata.reason.cons != NULL);
      SCIP_CALL( SCIPconsRelease(&boundchg->data.inferencedata.reason.cons, blkmem, set) );
      break;
      
   default:
      SCIPerrorMessage("invalid bound change type\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** creates empty domain change data with dynamic arrays */
static
SCIP_RETCODE domchgCreate(
   SCIP_DOMCHG**         domchg,             /**< pointer to domain change data */
   BMS_BLKMEM*           blkmem              /**< block memory */
   )
{
   assert(domchg != NULL);
   assert(blkmem != NULL);

   SCIP_ALLOC( BMSallocBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGDYN)) );
   (*domchg)->domchgdyn.domchgtype = SCIP_DOMCHGTYPE_DYNAMIC; /*lint !e641*/
   (*domchg)->domchgdyn.nboundchgs = 0;
   (*domchg)->domchgdyn.boundchgs = NULL;
   (*domchg)->domchgdyn.nholechgs = 0;
   (*domchg)->domchgdyn.holechgs = NULL;
   (*domchg)->domchgdyn.boundchgssize = 0;
   (*domchg)->domchgdyn.holechgssize = 0;

   return SCIP_OKAY;
}

/** frees domain change data */
SCIP_RETCODE SCIPdomchgFree(
   SCIP_DOMCHG**         domchg,             /**< pointer to domain change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(domchg != NULL);
   assert(blkmem != NULL);

   if( *domchg != NULL )
   {
      int i;

      /* release branching and inference data associated with the bound changes */
      for( i = 0; i < (int)(*domchg)->domchgbound.nboundchgs; ++i )
      {
         SCIP_CALL( boundchgReleaseData(&(*domchg)->domchgbound.boundchgs[i], blkmem, set) );
      }

      /* free memory for bound and hole changes */
      switch( (*domchg)->domchgdyn.domchgtype )
      {
      case SCIP_DOMCHGTYPE_BOUND:
         BMSfreeBlockMemoryArrayNull(blkmem, &(*domchg)->domchgbound.boundchgs, (*domchg)->domchgbound.nboundchgs);
         BMSfreeBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGBOUND));
         break;
      case SCIP_DOMCHGTYPE_BOTH:
         BMSfreeBlockMemoryArrayNull(blkmem, &(*domchg)->domchgboth.boundchgs, (*domchg)->domchgboth.nboundchgs);
         BMSfreeBlockMemoryArrayNull(blkmem, &(*domchg)->domchgboth.holechgs, (*domchg)->domchgboth.nholechgs);
         BMSfreeBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGBOTH));
         break;
      case SCIP_DOMCHGTYPE_DYNAMIC:
         BMSfreeBlockMemoryArrayNull(blkmem, &(*domchg)->domchgdyn.boundchgs, (*domchg)->domchgdyn.boundchgssize);
         BMSfreeBlockMemoryArrayNull(blkmem, &(*domchg)->domchgdyn.holechgs, (*domchg)->domchgdyn.holechgssize);
         BMSfreeBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGDYN));
         break;
      default:
         SCIPerrorMessage("invalid domain change type\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** converts a static domain change data into a dynamic one */
static
SCIP_RETCODE domchgMakeDynamic(
   SCIP_DOMCHG**         domchg,             /**< pointer to domain change data */
   BMS_BLKMEM*           blkmem              /**< block memory */
   )
{
   assert(domchg != NULL);
   assert(blkmem != NULL);

   SCIPdebugMessage("making domain change data %p pointing to %p dynamic\n", domchg, *domchg);

   if( *domchg == NULL )
   {
      SCIP_CALL( domchgCreate(domchg, blkmem) );
   }
   else
   {
      switch( (*domchg)->domchgdyn.domchgtype )
      {
      case SCIP_DOMCHGTYPE_BOUND:
         SCIP_ALLOC( BMSreallocBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGBOUND), sizeof(SCIP_DOMCHGDYN)) );
         (*domchg)->domchgdyn.nholechgs = 0;
         (*domchg)->domchgdyn.holechgs = NULL;
         (*domchg)->domchgdyn.boundchgssize = (*domchg)->domchgdyn.nboundchgs;
         (*domchg)->domchgdyn.holechgssize = 0;
         (*domchg)->domchgdyn.domchgtype = SCIP_DOMCHGTYPE_DYNAMIC; /*lint !e641*/
         break;
      case SCIP_DOMCHGTYPE_BOTH:
         SCIP_ALLOC( BMSreallocBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGBOTH), sizeof(SCIP_DOMCHGDYN)) );
         (*domchg)->domchgdyn.boundchgssize = (*domchg)->domchgdyn.nboundchgs;
         (*domchg)->domchgdyn.holechgssize = (*domchg)->domchgdyn.nholechgs;
         (*domchg)->domchgdyn.domchgtype = SCIP_DOMCHGTYPE_DYNAMIC; /*lint !e641*/
         break;
      case SCIP_DOMCHGTYPE_DYNAMIC:
         break;
      default:
         SCIPerrorMessage("invalid domain change type\n");
         return SCIP_INVALIDDATA;
      }
   }
#ifndef NDEBUG
   {
      int i;
      for( i = 0; i < (int)(*domchg)->domchgbound.nboundchgs; ++i )
         assert(SCIPvarGetType((*domchg)->domchgbound.boundchgs[i].var) == SCIP_VARTYPE_CONTINUOUS
            || EPSISINT((*domchg)->domchgbound.boundchgs[i].newbound, 1e-06));
   }
#endif

   return SCIP_OKAY;
}

/** converts a dynamic domain change data into a static one, using less memory than for a dynamic one */
SCIP_RETCODE SCIPdomchgMakeStatic(
   SCIP_DOMCHG**         domchg,             /**< pointer to domain change data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(domchg != NULL);
   assert(blkmem != NULL);

   SCIPdebugMessage("making domain change data %p pointing to %p static\n", domchg, *domchg);

   if( *domchg != NULL )
   {
      switch( (*domchg)->domchgdyn.domchgtype )
      {
      case SCIP_DOMCHGTYPE_BOUND:
         if( (*domchg)->domchgbound.nboundchgs == 0 )
         {
            SCIP_CALL( SCIPdomchgFree(domchg, blkmem, set) );
         }
         break;
      case SCIP_DOMCHGTYPE_BOTH:
         if( (*domchg)->domchgboth.nholechgs == 0 )
         {
            if( (*domchg)->domchgbound.nboundchgs == 0 )
            {
               SCIP_CALL( SCIPdomchgFree(domchg, blkmem, set) );
            }
            else
            {
               SCIP_ALLOC( BMSreallocBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGBOTH), sizeof(SCIP_DOMCHGBOUND)) );
               (*domchg)->domchgdyn.domchgtype = SCIP_DOMCHGTYPE_BOUND; /*lint !e641*/
            }
         }
         break;
      case SCIP_DOMCHGTYPE_DYNAMIC:
         if( (*domchg)->domchgboth.nholechgs == 0 )
         {
            if( (*domchg)->domchgbound.nboundchgs == 0 )
            {
               SCIP_CALL( SCIPdomchgFree(domchg, blkmem, set) );
            }
            else
            {
               /* shrink dynamic size arrays to their minimal sizes */
               SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &(*domchg)->domchgdyn.boundchgs,
                              (*domchg)->domchgdyn.boundchgssize, (*domchg)->domchgdyn.nboundchgs) );
               BMSfreeBlockMemoryArrayNull(blkmem, &(*domchg)->domchgdyn.holechgs, (*domchg)->domchgdyn.holechgssize);
            
               /* convert into static domain change */
               SCIP_ALLOC( BMSreallocBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGDYN), sizeof(SCIP_DOMCHGBOUND)) );
               (*domchg)->domchgdyn.domchgtype = SCIP_DOMCHGTYPE_BOUND; /*lint !e641*/
            }
         }
         else
         {
            /* shrink dynamic size arrays to their minimal sizes */
            SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &(*domchg)->domchgdyn.boundchgs,
                           (*domchg)->domchgdyn.boundchgssize, (*domchg)->domchgdyn.nboundchgs) );
            SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &(*domchg)->domchgdyn.holechgs,
                           (*domchg)->domchgdyn.holechgssize, (*domchg)->domchgdyn.nholechgs) );

            /* convert into static domain change */
            SCIP_ALLOC( BMSreallocBlockMemorySize(blkmem, domchg, sizeof(SCIP_DOMCHGDYN), sizeof(SCIP_DOMCHGBOTH)) );
            (*domchg)->domchgdyn.domchgtype = SCIP_DOMCHGTYPE_BOTH; /*lint !e641*/
         }
         break;
      default:
         SCIPerrorMessage("invalid domain change type\n");
         return SCIP_INVALIDDATA;
      }
#ifndef NDEBUG
      if( *domchg != NULL )
      {
         int i;
         for( i = 0; i < (int)(*domchg)->domchgbound.nboundchgs; ++i )
            assert(SCIPvarGetType((*domchg)->domchgbound.boundchgs[i].var) == SCIP_VARTYPE_CONTINUOUS
               || SCIPsetIsFeasIntegral(set, (*domchg)->domchgbound.boundchgs[i].newbound));
      }
#endif
   }

   return SCIP_OKAY;
}

/** ensures, that boundchgs array can store at least num entries */
static
SCIP_RETCODE domchgEnsureBoundchgsSize(
   SCIP_DOMCHG*          domchg,             /**< domain change data structure */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(domchg != NULL);
   assert(domchg->domchgdyn.domchgtype == SCIP_DOMCHGTYPE_DYNAMIC); /*lint !e641*/

   if( num > domchg->domchgdyn.boundchgssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &domchg->domchgdyn.boundchgs, domchg->domchgdyn.boundchgssize, newsize) );
      domchg->domchgdyn.boundchgssize = newsize;
   }
   assert(num <= domchg->domchgdyn.boundchgssize);

   return SCIP_OKAY;
}

/** ensures, that holechgs array can store at least num additional entries */
static
SCIP_RETCODE domchgEnsureHolechgsSize(
   SCIP_DOMCHG*          domchg,             /**< domain change data structure */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   num                 /**< minimum number of additional entries to store */
   )
{
   assert(domchg != NULL);
   assert(domchg->domchgdyn.domchgtype == SCIP_DOMCHGTYPE_DYNAMIC); /*lint !e641*/

   if( num > domchg->domchgdyn.holechgssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &domchg->domchgdyn.holechgs, domchg->domchgdyn.holechgssize, newsize) );
      domchg->domchgdyn.holechgssize = newsize;
   }
   assert(num <= domchg->domchgdyn.holechgssize);

   return SCIP_OKAY;
}

#if 0
static
SCIP_RETCODE domchgCleanupBdchgs(
   SCIP_DOMCHG*          domchg,             /**< domain change to apply */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   firstremoved        /**< array position of first deactivated bound change */
   )
{
   SCIP_VAR* var;
   int i;
   int j;
   int k;

   assert(domchg != NULL);
   assert(0 <= firstremoved && firstremoved < domchg->domchgbound.nboundchgs);
   assert(domchg->domchgbound.boundchgs[firstremoved].var == NULL);

#ifndef NDEBUG
   for( i = 0; i < firstremoved; ++i )
      assert(domchg->domchgbound.boundchgs[i].var != NULL);
#endif

   /* remove empty slots in bound change arrays by pushing the active bound changes to the front */
   i = firstremoved;
   for( j = i+1; j < domchg->domchgbound.nboundchgs; ++j )
   {
      var = domchg->domchgbound.boundchgs[j].var;
      if( var != NULL )
      {
         domchg->domchgbound.boundchgs[i] = domchg->domchgbound.boundchgs[j];
      }            
   }
}
#endif

/** shrinks bound change array of domain change to the given number of elements */
static
SCIP_RETCODE domchgShrinkBdchgs(
   SCIP_DOMCHG*          domchg,             /**< domain change to apply */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   int                   newnbdchgs          /**< new number of bound changes */
   )
{
   assert(domchg != NULL);
   assert(newnbdchgs < (int)domchg->domchgbound.nboundchgs);

   if( (SCIP_DOMCHGTYPE)domchg->domchgbound.domchgtype != SCIP_DOMCHGTYPE_DYNAMIC )
   {
      if( newnbdchgs == 0 )
      {
         BMSfreeBlockMemoryArray(blkmem, &domchg->domchgbound.boundchgs, domchg->domchgbound.nboundchgs);
      }
      else
      {
         SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &domchg->domchgbound.boundchgs, domchg->domchgbound.nboundchgs, 
               newnbdchgs) );
      }
   }
   domchg->domchgbound.nboundchgs = newnbdchgs; /*lint !e732*/
   assert((int)domchg->domchgbound.nboundchgs == newnbdchgs);

   return SCIP_OKAY;
}

/** applies domain change */
SCIP_RETCODE SCIPdomchgApply(
   SCIP_DOMCHG*          domchg,             /**< domain change to apply */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   int                   depth,              /**< depth in the tree, where the domain change takes place */
   SCIP_Bool*            cutoff              /**< pointer to store whether an infeasible domain change was detected */
   )
{
   int i;
   int j;

   assert(cutoff != NULL);

   *cutoff = FALSE;

   SCIPdebugMessage("applying domain changes at %p in depth %d\n", domchg, depth);
   if( domchg == NULL )
      return SCIP_OKAY;

   /* apply bound changes, removing inactive bound changes immediately */
   j = 0;
   for( i = 0; i < (int)domchg->domchgbound.nboundchgs; ++i )
   {
      assert(j <= i);
      SCIP_CALL( SCIPboundchgApply(&domchg->domchgbound.boundchgs[i], blkmem, set, stat, lp,
            branchcand, eventqueue, depth, j, cutoff) );
      if( *cutoff )
         break;
      if( domchg->domchgbound.boundchgs[i].var != NULL )
      {
         if( j < i )
            domchg->domchgbound.boundchgs[j] = domchg->domchgbound.boundchgs[i];
         j++;
      }
      else
      {
         /* release inference data of inactive bound change */
         SCIP_CALL( boundchgReleaseData(&domchg->domchgbound.boundchgs[i], blkmem, set) );
      }
   }
   SCIPdebugMessage(" -> %d bound changes (%d active)\n", domchg->domchgbound.nboundchgs, j);

   /* release inference data of all bound changes from the point on where a cutoff was detected */
   for( ; i < (int)domchg->domchgbound.nboundchgs; ++i )
   {
      assert(*cutoff);
      SCIP_CALL( boundchgReleaseData(&domchg->domchgbound.boundchgs[i], blkmem, set) );
   }

   /* clean up deactivated bound changes and all bound changes from the point on where a cutoff was detected */
   if( j < (int)domchg->domchgbound.nboundchgs )
   {
      SCIP_CALL( domchgShrinkBdchgs(domchg, blkmem, j) );
   }
   assert((int)domchg->domchgbound.nboundchgs == j);

   /* apply holelist changes */
   if( domchg->domchgdyn.domchgtype != SCIP_DOMCHGTYPE_BOUND ) /*lint !e641*/
   {
      for( i = 0; i < domchg->domchgboth.nholechgs; ++i )
         *(domchg->domchgboth.holechgs[i].ptr) = domchg->domchgboth.holechgs[i].newlist;
      SCIPdebugMessage(" -> %d hole changes\n", domchg->domchgboth.nholechgs);
   }

   return SCIP_OKAY;
}
   
/** undoes domain change */
SCIP_RETCODE SCIPdomchgUndo(
   SCIP_DOMCHG*          domchg,             /**< domain change to remove */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   )
{
   int i;

   SCIPdebugMessage("undoing domain changes at %p\n", domchg);
   if( domchg == NULL )
      return SCIP_OKAY;

   /* undo holelist changes */
   if( domchg->domchgdyn.domchgtype != SCIP_DOMCHGTYPE_BOUND ) /*lint !e641*/
   {
      for( i = domchg->domchgboth.nholechgs-1; i >= 0; --i )
         *(domchg->domchgboth.holechgs[i].ptr) = domchg->domchgboth.holechgs[i].oldlist;
      SCIPdebugMessage(" -> %d hole changes\n", domchg->domchgboth.nholechgs);
   }

   /* undo bound changes */
   for( i = domchg->domchgbound.nboundchgs-1; i >= 0; --i )
   {
      SCIP_CALL( SCIPboundchgUndo(&domchg->domchgbound.boundchgs[i], blkmem, set, stat, lp, branchcand, eventqueue) );
   }
   SCIPdebugMessage(" -> %d bound changes\n", domchg->domchgbound.nboundchgs);

   return SCIP_OKAY;
}

/** adds bound change to domain changes */
SCIP_RETCODE SCIPdomchgAddBoundchg(
   SCIP_DOMCHG**         domchg,             /**< pointer to domain change data structure */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR*             var,                /**< variable to change the bounds for */
   SCIP_Real             newbound,           /**< new value for bound */
   SCIP_BOUNDTYPE        boundtype,          /**< type of bound for var: lower or upper bound */
   SCIP_BOUNDCHGTYPE     boundchgtype,       /**< type of bound change: branching decision or inference */
   SCIP_Real             lpsolval,           /**< solval of variable in last LP on path to node, or SCIP_INVALID if unknown */
   SCIP_VAR*             infervar,           /**< variable that was changed (parent of var, or var itself), or NULL */
   SCIP_CONS*            infercons,          /**< constraint that deduced the bound change, or NULL */
   SCIP_PROP*            inferprop,          /**< propagator that deduced the bound change, or NULL */
   int                   inferinfo,          /**< user information for inference to help resolving the conflict */
   SCIP_BOUNDTYPE        inferboundtype      /**< type of bound for inference var: lower or upper bound */
   )
{
   SCIP_BOUNDCHG* boundchg;

   assert(domchg != NULL);
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS || SCIPsetIsFeasIntegral(set, newbound));
   assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY
      || SCIPsetIsEQ(set, newbound, boundtype == SCIP_BOUNDTYPE_LOWER ? 1.0 : 0.0));
   assert(boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING || infervar != NULL);
   assert((boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER) == (infercons != NULL));
   assert(boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER || inferprop == NULL);

   SCIPdebugMessage("adding %s bound change <%s: %g> of variable <%s> to domain change at %p pointing to %p\n",
      boundtype == SCIP_BOUNDTYPE_LOWER ? "lower" : "upper", 
      boundchgtype == SCIP_BOUNDCHGTYPE_BRANCHING ? "branching" : "inference", 
      newbound, var->name, domchg, *domchg);

   /* if domain change data doesn't exist, create it;
    * if domain change is static, convert it into dynamic change
    */
   if( *domchg == NULL )
   {
      SCIP_CALL( domchgCreate(domchg, blkmem) );
   }
   else if( (*domchg)->domchgdyn.domchgtype != SCIP_DOMCHGTYPE_DYNAMIC ) /*lint !e641*/
   {
      SCIP_CALL( domchgMakeDynamic(domchg, blkmem) );
   }
   assert(*domchg != NULL && (*domchg)->domchgdyn.domchgtype == SCIP_DOMCHGTYPE_DYNAMIC); /*lint !e641*/

   /* get memory for additional bound change */
   SCIP_CALL( domchgEnsureBoundchgsSize(*domchg, blkmem, set, (*domchg)->domchgdyn.nboundchgs+1) );

   /* fill in the bound change data */
   boundchg = &(*domchg)->domchgdyn.boundchgs[(*domchg)->domchgdyn.nboundchgs];
   boundchg->var = var;
   switch( boundchgtype )
   {
   case SCIP_BOUNDCHGTYPE_BRANCHING:
      boundchg->data.branchingdata.lpsolval = lpsolval;
      break;
   case SCIP_BOUNDCHGTYPE_CONSINFER:
      assert(infercons != NULL);
      boundchg->data.inferencedata.var = infervar;
      boundchg->data.inferencedata.reason.cons = infercons;
      boundchg->data.inferencedata.info = inferinfo; 
      break;
   case SCIP_BOUNDCHGTYPE_PROPINFER:
      boundchg->data.inferencedata.var = infervar;
      boundchg->data.inferencedata.reason.prop = inferprop;
      boundchg->data.inferencedata.info = inferinfo; 
      break;
   default:
      SCIPerrorMessage("invalid bound change type %d\n", boundchgtype);
      return SCIP_INVALIDDATA;
   }

   boundchg->newbound = newbound;
   boundchg->boundchgtype = boundchgtype; /*lint !e641*/
   boundchg->boundtype = boundtype; /*lint !e641*/
   boundchg->inferboundtype = inferboundtype; /*lint !e641*/
   boundchg->applied = FALSE;
   (*domchg)->domchgdyn.nboundchgs++;

   /* capture branching and inference data associated with the bound changes */
   SCIP_CALL( boundchgCaptureData(boundchg) );

#if 0
#ifndef NDEBUG
   {
      int i;
      for( i = 0; i < (int)(*domchg)->domchgbound.nboundchgs; ++i )
         assert(SCIPvarGetType((*domchg)->domchgbound.boundchgs[i].var) == SCIP_VARTYPE_CONTINUOUS
            || SCIPsetIsFeasIntegral(set, (*domchg)->domchgbound.boundchgs[i].newbound));
   }
#endif
#endif

   return SCIP_OKAY;
}

/** adds hole change to domain changes */
SCIP_RETCODE SCIPdomchgAddHolechg(
   SCIP_DOMCHG**         domchg,             /**< pointer to domain change data structure */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_HOLELIST**       ptr,                /**< changed list pointer */
   SCIP_HOLELIST*        newlist,            /**< new value of list pointer */
   SCIP_HOLELIST*        oldlist             /**< old value of list pointer */
   )
{
   SCIP_HOLECHG* holechg;

   assert(domchg != NULL);
   assert(ptr != NULL);

   /* if domain change data doesn't exist, create it;
    * if domain change is static, convert it into dynamic change
    */
   if( *domchg == NULL )
   {
      SCIP_CALL( domchgCreate(domchg, blkmem) );
   }
   else if( (*domchg)->domchgdyn.domchgtype != SCIP_DOMCHGTYPE_DYNAMIC ) /*lint !e641*/
   {
      SCIP_CALL( domchgMakeDynamic(domchg, blkmem) );
   }
   assert(*domchg != NULL && (*domchg)->domchgdyn.domchgtype == SCIP_DOMCHGTYPE_DYNAMIC); /*lint !e641*/

   /* get memory for additional hole change */
   SCIP_CALL( domchgEnsureHolechgsSize(*domchg, blkmem, set, (*domchg)->domchgdyn.nholechgs+1) );

   /* fill in the hole change data */
   holechg = &(*domchg)->domchgdyn.holechgs[(*domchg)->domchgdyn.nholechgs];
   holechg->ptr = ptr;
   holechg->newlist = newlist;
   holechg->oldlist = oldlist;
   (*domchg)->domchgdyn.nholechgs++;

   return SCIP_OKAY;
}




/*
 * methods for variables 
 */

/** returns adjusted lower bound value, which is rounded for integral variable types */
static
SCIP_Real adjustedLb(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VARTYPE          vartype,            /**< type of variable */
   SCIP_Real             lb                  /**< lower bound to adjust */
   )
{
   if( SCIPsetIsInfinity(set, -lb) )
      return -SCIPsetInfinity(set);
   else if( vartype != SCIP_VARTYPE_CONTINUOUS )
      return SCIPsetFeasCeil(set, lb);
   else if( SCIPsetIsZero(set, lb) )
      return 0.0;
   else
      return lb;
}

/** returns adjusted upper bound value, which is rounded for integral variable types */
static
SCIP_Real adjustedUb(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VARTYPE          vartype,            /**< type of variable */
   SCIP_Real             ub                  /**< upper bound to adjust */
   )
{
   if( SCIPsetIsInfinity(set, ub) )
      return SCIPsetInfinity(set);
   else if( vartype != SCIP_VARTYPE_CONTINUOUS )
      return SCIPsetFeasFloor(set, ub);
   else if( SCIPsetIsZero(set, ub) )
      return 0.0;
   else
      return ub;
}

/* removes (redundant) implications and variable bounds of variable from all other variables' implications and variable
 * bounds arrays, and optionally removes them also from the variable itself
 */
static
SCIP_RETCODE varRemoveImplicsVbs(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Bool             onlyredundant,      /**< should only the redundant implications and variable bounds be removed? */
   SCIP_Bool             removefromvar       /**< should the implications and variable bounds be removed from the var itself? */
   )
{
   SCIP_Real lb;
   SCIP_Real ub;

   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);
   assert(SCIPvarIsActive(var));

   lb = SCIPvarGetLbGlobal(var);
   ub = SCIPvarGetUbGlobal(var);

   SCIPdebugMessage("removing %s implications and vbounds of <%s>[%g,%g]\n",
      onlyredundant ? "redundant" : "all", SCIPvarGetName(var), lb, ub);

   /* remove implications of (fixed) binary variable */
   if( var->implics != NULL && (!onlyredundant || lb > 0.5 || ub < 0.5) )
   {
      SCIP_Bool varfixing;

      assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);

      varfixing = FALSE;
      do
      {
         int nimpls;
         int nbinimpls;
         SCIP_VAR** implvars;
         SCIP_BOUNDTYPE* impltypes;
         int i;

         nimpls = SCIPimplicsGetNImpls(var->implics, varfixing);
         nbinimpls = SCIPimplicsGetNBinImpls(var->implics, varfixing);
         implvars = SCIPimplicsGetVars(var->implics, varfixing);
         impltypes = SCIPimplicsGetTypes(var->implics, varfixing);

         /* process the implications on binary variables */
         for( i = 0; i < nbinimpls; i++ )
         {
            SCIP_VAR* implvar;
            SCIP_BOUNDTYPE impltype;

            implvar = implvars[i];
            impltype = impltypes[i];
            assert(implvar != var);
            assert(SCIPvarGetType(implvar) == SCIP_VARTYPE_BINARY);

            /* remove for all implications z == 0 / 1  ==>  x <= 0 / x >= 1 (x binary)
             * the following implication from x's implications 
             *   x == 1  ==>  z <= 0            , for z == 1  ==>  x <= 0
             *   x == 0  ==>  z <= 0            , for z == 1  ==>  x >= 1
             *
             *   x == 1  ==>  z >= 1            , for z == 0  ==>  x <= 0
             *   x == 0  ==>  z >= 1            , for z == 0  ==>  x >= 1
             */
            if( implvar->implics != NULL ) /* implvar may have been aggregated in the mean time */
            {
               SCIPdebugMessage("deleting implication: <%s> == %d  ==>  <%s> %s\n", 
                  SCIPvarGetName(implvar), (impltype == SCIP_BOUNDTYPE_UPPER),
                  SCIPvarGetName(var), varfixing ? "<= 0 " : ">= 1");
               SCIP_CALL( SCIPimplicsDel(&implvar->implics, blkmem, set, (impltype == SCIP_BOUNDTYPE_UPPER), var, 
                     varfixing ? SCIP_BOUNDTYPE_UPPER : SCIP_BOUNDTYPE_LOWER) );
            }
         }

         /* process the implications on non-binary variables */
         for( i = nbinimpls; i < nimpls; i++ )
         {
            SCIP_VAR* implvar;
            SCIP_BOUNDTYPE impltype;

            implvar = implvars[i];
            impltype = impltypes[i];
            assert(implvar != var);
            assert(SCIPvarGetType(implvar) != SCIP_VARTYPE_BINARY);

            /* remove for all implications z == 0 / 1  ==>  x <= p / x >= p (x not binary)
             * the following variable bound from x's variable bounds 
             *   x <= b*z+d (z in vubs of x)            , for z == 0 / 1  ==>  x <= p
             *   x >= b*z+d (z in vlbs of x)            , for z == 0 / 1  ==>  x >= p
             */
            if( impltype == SCIP_BOUNDTYPE_UPPER )
            {
               if( implvar->vubs != NULL ) /* implvar may have been aggregated in the mean time */
               {
                  SCIPdebugMessage("deleting variable bound: <%s> == %d  ==>  <%s> <= %g\n", 
                     SCIPvarGetName(var), varfixing, SCIPvarGetName(implvar), 
                     SCIPimplicsGetBounds(var->implics, varfixing)[i]);
                  SCIP_CALL( SCIPvboundsDel(&implvar->vubs, blkmem, var) );
               }
            }
            else
            {
               if( implvar->vlbs != NULL ) /* implvar may have been aggregated in the mean time */
               {
                  SCIPdebugMessage("deleting variable bound: <%s> == %d  ==>  <%s> >= %g\n", 
                     SCIPvarGetName(var), varfixing, SCIPvarGetName(implvar), 
                     SCIPimplicsGetBounds(var->implics, varfixing)[i]);
                  SCIP_CALL( SCIPvboundsDel(&implvar->vlbs, blkmem, var) );
               }
            }
         }
         varfixing = !varfixing;
      }
      while( varfixing == TRUE );

      if( removefromvar )
      {
         /* free the implications data structures */
         SCIPimplicsFree(&var->implics, blkmem);
      }
   }

   /* remove the (redundant) variable lower bounds */
   if( var->vlbs != NULL )
   {
      SCIP_VAR** vars;
      SCIP_Real* coefs;
      SCIP_Real* constants;
      int nvbds;
      int newnvbds;
      int i;

      assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY);

      nvbds = SCIPvboundsGetNVbds(var->vlbs);
      vars = SCIPvboundsGetVars(var->vlbs);
      coefs = SCIPvboundsGetCoefs(var->vlbs);
      constants = SCIPvboundsGetConstants(var->vlbs);

      /* remove for all variable bounds x >= b*z+d the following implication from z's implications 
       *   z == 1  ==>  x >= b + d           , if b > 0
       *   z == 0  ==>  x >= d               , if b < 0
       */
      newnvbds = 0;
      for( i = 0; i < nvbds; i++ )
      {
         SCIP_Real coef;

         assert(newnvbds <= i);
         coef = coefs[i];
         assert(!SCIPsetIsZero(set, coef));

         /* check, if we want to remove the variable bound */
         if( onlyredundant )
         {
            SCIP_Real vbound;

            vbound = MAX(coef, 0.0) + constants[i];
            if( SCIPsetIsFeasGT(set, vbound, lb) )
            {
               /* the variable bound is not redundant: keep it */
               if( removefromvar )
               {
                  if( newnvbds < i )
                  {
                     vars[newnvbds] = vars[i];
                     coefs[newnvbds] = coefs[i];
                     constants[newnvbds] = constants[i];
                  }
                  newnvbds++;
               }
               continue;
            }
         }

         /* remove the corresponding implication */
         if( vars[i]->implics != NULL ) /* variable may have been aggregated in the mean time */
         {
            SCIPdebugMessage("deleting implication: <%s> == %d  ==>  <%s> >= %g\n", 
               SCIPvarGetName(vars[i]), (coef > 0.0), SCIPvarGetName(var), MAX(coef, 0.0) + constants[i]);
            SCIP_CALL( SCIPimplicsDel(&vars[i]->implics, blkmem, set, (coef > 0.0), var, SCIP_BOUNDTYPE_LOWER) );
         }
      }

      if( removefromvar )
      {
         /* update the number of variable bounds */
         SCIPvboundsShrink(&var->vlbs, blkmem, newnvbds);
      }
   }

   /* remove the (redundant) variable upper bounds */
   if( var->vubs != NULL )
   {
      SCIP_VAR** vars;
      SCIP_Real* coefs;
      SCIP_Real* constants;
      int nvbds;
      int newnvbds;
      int i;

      assert(SCIPvarGetType(var) != SCIP_VARTYPE_BINARY);

      nvbds = SCIPvboundsGetNVbds(var->vubs);
      vars = SCIPvboundsGetVars(var->vubs);
      coefs = SCIPvboundsGetCoefs(var->vubs);
      constants = SCIPvboundsGetConstants(var->vubs);

      /* remove for all variable bounds x <= b*z+d the following implication from z's implications 
       *   z == 0  ==>  x <= d               , if b > 0
       *   z == 1  ==>  x <= b + d           , if b < 0
       */
      newnvbds = 0;
      for( i = 0; i < nvbds; i++ )
      {
         SCIP_Real coef;

         assert(newnvbds <= i);
         coef = coefs[i];
         assert(!SCIPsetIsZero(set, coef));

         /* check, if we want to remove the variable bound */
         if( onlyredundant )
         {
            SCIP_Real vbound;

            vbound = MIN(coef, 0.0) + constants[i];
            if( SCIPsetIsFeasLT(set, vbound, ub) )
            {
               /* the variable bound is not redundant: keep it */
               if( removefromvar )
               {
                  if( newnvbds < i )
                  {
                     vars[newnvbds] = vars[i];
                     coefs[newnvbds] = coefs[i];
                     constants[newnvbds] = constants[i];
                  }
                  newnvbds++;
               }
               continue;
            }
         }

         /* remove the corresponding implication */
         if( vars[i]->implics != NULL ) /* variable may have been aggregated in the mean time */
         {
            SCIPdebugMessage("deleting implication: <%s> == %d  ==>  <%s> <= %g\n", 
               SCIPvarGetName(vars[i]), (coef < 0.0), SCIPvarGetName(var), MIN(coef, 0.0) + constants[i]);
            SCIP_CALL( SCIPimplicsDel(&vars[i]->implics, blkmem, set, (coef < 0.0), var, SCIP_BOUNDTYPE_UPPER) );
         }
      }

      if( removefromvar )
      {
         /* update the number of variable bounds */
         SCIPvboundsShrink(&var->vubs, blkmem, newnvbds);
      }
   }

   return SCIP_OKAY;
}

/** creates variable; if variable is of integral type, fractional bounds are automatically rounded; an integer variable
 *  with bounds zero and one is automatically converted into a binary variable
 */
static
SCIP_RETCODE varCreate(
   SCIP_VAR**            var,                /**< pointer to variable data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   const char*           name,               /**< name of variable, or NULL for automatic name creation */
   SCIP_Real             lb,                 /**< lower bound of variable */
   SCIP_Real             ub,                 /**< upper bound of variable */
   SCIP_Real             obj,                /**< objective function value */
   SCIP_VARTYPE          vartype,            /**< type of variable */
   SCIP_Bool             initial,            /**< should var's column be present in the initial root LP? */
   SCIP_Bool             removeable,         /**< is var's column removeable from the LP (due to aging or cleanup)? */
   SCIP_DECL_VARDELORIG  ((*vardelorig)),    /**< frees user data of original variable */
   SCIP_DECL_VARTRANS    ((*vartrans)),      /**< creates transformed user data by transforming original user data */
   SCIP_DECL_VARDELTRANS ((*vardeltrans)),   /**< frees user data of transformed variable */
   SCIP_VARDATA*         vardata             /**< user data for this specific variable */
   )
{
   assert(var != NULL);
   assert(blkmem != NULL);
   assert(stat != NULL);

   /* adjust bounds of variable */
   lb = adjustedLb(set, vartype, lb);
   ub = adjustedUb(set, vartype, ub);
   
   /* convert [0,1]-integers into binary variables */
   if( vartype == SCIP_VARTYPE_INTEGER
      && (SCIPsetIsEQ(set, lb, 0.0) || SCIPsetIsEQ(set, lb, 1.0))
      && (SCIPsetIsEQ(set, ub, 0.0) || SCIPsetIsEQ(set, ub, 1.0)) )
      vartype = SCIP_VARTYPE_BINARY;

   assert(vartype != SCIP_VARTYPE_BINARY || SCIPsetIsEQ(set, lb, 0.0) || SCIPsetIsEQ(set, lb, 1.0));
   assert(vartype != SCIP_VARTYPE_BINARY || SCIPsetIsEQ(set, ub, 0.0) || SCIPsetIsEQ(set, ub, 1.0));

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, var) );

   if( name == NULL )
   {
      char s[SCIP_MAXSTRLEN];
      sprintf(s, "_var%d_", stat->nvaridx);
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*var)->name, s, strlen(s)+1) );
   }
   else
   {
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*var)->name, name, strlen(name)+1) );
   }
   (*var)->obj = obj;
   (*var)->branchfactor = 1.0;
   (*var)->rootsol = 0.0;
   (*var)->primsolavg = 0.5 * (lb + ub);
   (*var)->glbdom.holelist = NULL;
   (*var)->glbdom.lb = lb;
   (*var)->glbdom.ub = ub;
   (*var)->locdom.holelist = NULL;
   (*var)->locdom.lb = lb;
   (*var)->locdom.ub = ub;
   (*var)->vardelorig = vardelorig;
   (*var)->vartrans = vartrans;
   (*var)->vardeltrans = vardeltrans;
   (*var)->vardata = vardata;
   (*var)->parentvars = NULL;
   (*var)->negatedvar = NULL;
   (*var)->vlbs = NULL;
   (*var)->vubs = NULL;
   (*var)->implics = NULL;
   (*var)->cliquelist = NULL;
   (*var)->eventfilter = NULL;
   (*var)->lbchginfos = NULL;
   (*var)->ubchginfos = NULL;
   (*var)->index = stat->nvaridx++;
   (*var)->probindex = -1;
   (*var)->pseudocandindex = -1;
   (*var)->eventqueueindexobj = -1;
   (*var)->eventqueueindexlb = -1;
   (*var)->eventqueueindexub = -1;
   (*var)->parentvarssize = 0;
   (*var)->nparentvars = 0;
   (*var)->nuses = 0;
   (*var)->nlocksdown = 0;
   (*var)->nlocksup = 0;
   (*var)->branchpriority = 0;
   (*var)->branchdirection = SCIP_BRANCHDIR_AUTO; /*lint !e641*/
   (*var)->lbchginfossize = 0;
   (*var)->nlbchginfos = 0;
   (*var)->ubchginfossize = 0;
   (*var)->nubchginfos = 0;
   (*var)->conflictsetcount = 0;
   (*var)->conflictqueuecount = 0;
   (*var)->initial = initial;
   (*var)->removeable = removeable;
   (*var)->deleted = FALSE;
   (*var)->vartype = vartype; /*lint !e641*/
   (*var)->pseudocostflag = FALSE;
   (*var)->eventqueueimpl = FALSE;

   /* create branching and inference history entries */
   SCIP_CALL( SCIPhistoryCreate(&(*var)->history, blkmem) );
   SCIP_CALL( SCIPhistoryCreate(&(*var)->historycrun, blkmem) );

   return SCIP_OKAY;
}

/** creates and captures an original problem variable; an integer variable with bounds
 *  zero and one is automatically converted into a binary variable
 */
SCIP_RETCODE SCIPvarCreateOriginal(
   SCIP_VAR**            var,                /**< pointer to variable data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   const char*           name,               /**< name of variable, or NULL for automatic name creation */
   SCIP_Real             lb,                 /**< lower bound of variable */
   SCIP_Real             ub,                 /**< upper bound of variable */
   SCIP_Real             obj,                /**< objective function value */
   SCIP_VARTYPE          vartype,            /**< type of variable */
   SCIP_Bool             initial,            /**< should var's column be present in the initial root LP? */
   SCIP_Bool             removeable,         /**< is var's column removeable from the LP (due to aging or cleanup)? */
   SCIP_DECL_VARDELORIG  ((*vardelorig)),    /**< frees user data of original variable */
   SCIP_DECL_VARTRANS    ((*vartrans)),      /**< creates transformed user data by transforming original user data */
   SCIP_DECL_VARDELTRANS ((*vardeltrans)),   /**< frees user data of transformed variable */
   SCIP_VARDATA*         vardata             /**< user data for this specific variable */
   )
{
   assert(var != NULL);
   assert(blkmem != NULL);
   assert(stat != NULL);

   /* create variable */
   SCIP_CALL( varCreate(var, blkmem, set, stat, name, lb, ub, obj, vartype, initial, removeable,
         vardelorig, vartrans, vardeltrans, vardata) );

   /* set variable status and data */
   (*var)->varstatus = SCIP_VARSTATUS_ORIGINAL; /*lint !e641*/
   (*var)->data.original.origdom.holelist = NULL;
   (*var)->data.original.origdom.lb = lb;
   (*var)->data.original.origdom.ub = ub;
   (*var)->data.original.transvar = NULL;

   /* capture variable */
   SCIPvarCapture(*var);

   return SCIP_OKAY;
}

/** creates and captures a loose variable belonging to the transformed problem; an integer variable with bounds
 *  zero and one is automatically converted into a binary variable
 */
SCIP_RETCODE SCIPvarCreateTransformed(
   SCIP_VAR**            var,                /**< pointer to variable data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   const char*           name,               /**< name of variable, or NULL for automatic name creation */
   SCIP_Real             lb,                 /**< lower bound of variable */
   SCIP_Real             ub,                 /**< upper bound of variable */
   SCIP_Real             obj,                /**< objective function value */
   SCIP_VARTYPE          vartype,            /**< type of variable */
   SCIP_Bool             initial,            /**< should var's column be present in the initial root LP? */
   SCIP_Bool             removeable,         /**< is var's column removeable from the LP (due to aging or cleanup)? */
   SCIP_DECL_VARDELORIG  ((*vardelorig)),    /**< frees user data of original variable */
   SCIP_DECL_VARTRANS    ((*vartrans)),      /**< creates transformed user data by transforming original user data */
   SCIP_DECL_VARDELTRANS ((*vardeltrans)),   /**< frees user data of transformed variable */
   SCIP_VARDATA*         vardata             /**< user data for this specific variable */
   )
{
   assert(var != NULL);
   assert(blkmem != NULL);

   /* create variable */
   SCIP_CALL( varCreate(var, blkmem, set, stat, name, lb, ub, obj, vartype, initial, removeable,
         vardelorig, vartrans, vardeltrans, vardata) );

   /* create event filter for transformed variable */
   SCIP_CALL( SCIPeventfilterCreate(&(*var)->eventfilter, blkmem) );

   /* set variable status and data */
   (*var)->varstatus = SCIP_VARSTATUS_LOOSE; /*lint !e641*/

   /* capture variable */
   SCIPvarCapture(*var);

   return SCIP_OKAY;   
}

/** ensures, that parentvars array of var can store at least num entries */
static
SCIP_RETCODE varEnsureParentvarsSize(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(var->nparentvars <= var->parentvarssize);
   
   if( num > var->parentvarssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &var->parentvars, var->parentvarssize, newsize) );
      var->parentvarssize = newsize;
   }
   assert(num <= var->parentvarssize);

   return SCIP_OKAY;
}

/** adds variable to parent list of a variable and captures parent variable */
static
SCIP_RETCODE varAddParent(
   SCIP_VAR*             var,                /**< variable to add parent to */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR*             parentvar           /**< parent variable to add */
   )
{
   assert(var != NULL);
   assert(parentvar != NULL);
   /* the direct original counterpart must be stored as first parent */
   assert(var->nparentvars == 0 || SCIPvarGetStatus(parentvar) != SCIP_VARSTATUS_ORIGINAL);

   SCIPdebugMessage("adding parent <%s>[%p] to variable <%s>[%p] in slot %d\n", 
      parentvar->name, parentvar, var->name, var, var->nparentvars);

   SCIP_CALL( varEnsureParentvarsSize(var, blkmem, set, var->nparentvars+1) );

   var->parentvars[var->nparentvars] = parentvar;
   var->nparentvars++;

   SCIPvarCapture(parentvar);

   return SCIP_OKAY;
}

/** deletes and releases all variables from the parent list of a variable, frees the memory of parents array */
static
SCIP_RETCODE varFreeParents(
   SCIP_VAR**            var,                /**< pointer to variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp                  /**< current LP data (or NULL, if it's an original variable) */
   )
{
   SCIP_VAR* parentvar;
   int i;

   SCIPdebugMessage("free parents of <%s>\n", (*var)->name);

   /* release the parent variables and remove the link from the parent variable to the child */
   for( i = 0; i < (*var)->nparentvars; ++i )
   {
      assert((*var)->parentvars != NULL);
      parentvar = (*var)->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         assert(parentvar->data.original.transvar == *var);
         assert(&parentvar->data.original.transvar != var);
         parentvar->data.original.transvar = NULL;
         break;

      case SCIP_VARSTATUS_AGGREGATED:
         assert(parentvar->data.aggregate.var == *var);
         assert(&parentvar->data.aggregate.var != var);
         parentvar->data.aggregate.var = NULL;
         break;

#if 0
      case SCIP_VARSTATUS_MULTAGGR:
         assert(parentvar->data.multaggr.vars != NULL);
         for( v = 0; v < parentvar->data.multaggr.nvars && parentvar->data.multaggr.vars[v] != *var; ++v )
         {}
         assert(v < parentvar->data.multaggr.nvars && parentvar->data.multaggr.vars[v] == *var);
         if( v < parentvar->data.multaggr.nvars-1 )
         {
            parentvar->data.multaggr.vars[v] = parentvar->data.multaggr.vars[parentvar->data.multaggr.nvars-1];
            parentvar->data.multaggr.scalars[v] = parentvar->data.multaggr.scalars[parentvar->data.multaggr.nvars-1];
         }
         parentvar->data.multaggr.nvars--;
         break;
#endif

      case SCIP_VARSTATUS_NEGATED:
         assert(parentvar->negatedvar == *var);
         assert((*var)->negatedvar == parentvar);
         parentvar->negatedvar = NULL;
         (*var)->negatedvar = NULL;
         break;

      default:
         SCIPerrorMessage("parent variable is neither ORIGINAL, AGGREGATED nor NEGATED\n");
         return SCIP_INVALIDDATA;
      }  /*lint !e788*/

      SCIP_CALL( SCIPvarRelease(&(*var)->parentvars[i], blkmem, set, lp) );
   }

   /* free parentvars array */
   BMSfreeBlockMemoryArrayNull(blkmem, &(*var)->parentvars, (*var)->parentvarssize);

   return SCIP_OKAY;
}

/** frees a variable */
static
SCIP_RETCODE varFree(
   SCIP_VAR**            var,                /**< pointer to variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp                  /**< current LP data (may be NULL, if it's not a column variable) */
   )
{
   assert(blkmem != NULL);
   assert(var != NULL);
   assert(*var != NULL);
   assert(SCIPvarGetStatus(*var) != SCIP_VARSTATUS_COLUMN || &(*var)->data.col->var != var);
   assert((*var)->nuses == 0);
   assert((*var)->probindex == -1);

   SCIPdebugMessage("free variable <%s> with status=%d\n", (*var)->name, SCIPvarGetStatus(*var));
   switch( SCIPvarGetStatus(*var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert((*var)->data.original.transvar == NULL); /* cannot free variable, if transformed variable is still existing */
      holelistFree(&(*var)->data.original.origdom.holelist, blkmem);
      break;
   case SCIP_VARSTATUS_LOOSE:
      break;
   case SCIP_VARSTATUS_COLUMN:
      SCIP_CALL( SCIPcolFree(&(*var)->data.col, blkmem, set, lp) );  /* free corresponding LP column */
      break;
   case SCIP_VARSTATUS_FIXED:
   case SCIP_VARSTATUS_AGGREGATED:
      break;
   case SCIP_VARSTATUS_MULTAGGR:
      BMSfreeBlockMemoryArray(blkmem, &(*var)->data.multaggr.vars, (*var)->data.multaggr.varssize);
      BMSfreeBlockMemoryArray(blkmem, &(*var)->data.multaggr.scalars, (*var)->data.multaggr.varssize);
      break;
   case SCIP_VARSTATUS_NEGATED:
      break;
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   /* release all parent variables and free the parentvars array */
   SCIP_CALL( varFreeParents(var, blkmem, set, lp) );

   /* free user data */
   if( SCIPvarGetStatus(*var) == SCIP_VARSTATUS_ORIGINAL )
   {
      if( (*var)->vardelorig != NULL )
      {
         SCIP_CALL( (*var)->vardelorig(set->scip, *var, &(*var)->vardata) );
      }
   }
   else
   {
      if( (*var)->vardeltrans != NULL )
      {
         SCIP_CALL( (*var)->vardeltrans(set->scip, *var, &(*var)->vardata) );
      }
   }

   /* free event filter */
   if( (*var)->eventfilter != NULL )
   {
      SCIP_CALL( SCIPeventfilterFree(&(*var)->eventfilter, blkmem, set) );
   }
   assert((*var)->eventfilter == NULL);

   /* free hole lists */
   holelistFree(&(*var)->glbdom.holelist, blkmem);
   holelistFree(&(*var)->locdom.holelist, blkmem);

   /* free variable bounds data structures */
   SCIPvboundsFree(&(*var)->vlbs, blkmem);
   SCIPvboundsFree(&(*var)->vubs, blkmem);

   /* free implications data structures */
   SCIPimplicsFree(&(*var)->implics, blkmem);

   /* free clique list data structures */
   SCIPcliquelistFree(&(*var)->cliquelist, blkmem);

   /* free bound change information arrays */
   BMSfreeBlockMemoryArrayNull(blkmem, &(*var)->lbchginfos, (*var)->lbchginfossize);
   BMSfreeBlockMemoryArrayNull(blkmem, &(*var)->ubchginfos, (*var)->ubchginfossize);

   /* free branching and inference history entries */
   SCIPhistoryFree(&(*var)->history, blkmem);
   SCIPhistoryFree(&(*var)->historycrun, blkmem);

   /* free variable data structure */
   BMSfreeBlockMemoryArray(blkmem, &(*var)->name, strlen((*var)->name)+1);
   BMSfreeBlockMemory(blkmem, var);

   return SCIP_OKAY;
}

/** increases usage counter of variable */
void SCIPvarCapture(
   SCIP_VAR*             var                 /**< variable */
   )
{
   assert(var != NULL);
   assert(var->nuses >= 0);

   SCIPdebugMessage("capture variable <%s> with nuses=%d\n", var->name, var->nuses);
   var->nuses++;
}

/** decreases usage counter of variable, and frees memory if necessary */
SCIP_RETCODE SCIPvarRelease(
   SCIP_VAR**            var,                /**< pointer to variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp                  /**< current LP data (or NULL, if it's an original variable) */
   )
{
   assert(blkmem != NULL);
   assert(var != NULL);
   assert(*var != NULL);
   assert((*var)->nuses >= 1);

   SCIPdebugMessage("release variable <%s> with nuses=%d\n", (*var)->name, (*var)->nuses);
   (*var)->nuses--;
   if( (*var)->nuses == 0 )
   {
      SCIP_CALL( varFree(var, blkmem, set, lp) );
   }

   *var = NULL;

   return SCIP_OKAY;
}

/** initializes variable data structure for solving */
void SCIPvarInitSolve(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   SCIPhistoryReset(var->historycrun);
   var->conflictsetcount = 0;
   var->conflictqueuecount = 0;

   /* the negations of active problem variables can also be member of a conflict set */
   if( var->negatedvar != NULL )
   {
      var->negatedvar->conflictsetcount = 0;
      var->negatedvar->conflictqueuecount = 0;
   }
}

/** outputs variable information into file stream */
void SCIPvarPrint(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   FILE*                 file                /**< output file (or NULL for standard output) */
   )
{
   SCIP_Real lb;
   SCIP_Real ub;
   int i;

   assert(var != NULL);

   /* type of variable */
   switch( SCIPvarGetType(var) )
   {
   case SCIP_VARTYPE_BINARY:
      SCIPmessageFPrintInfo(file, "  [binary]");
      break;
   case SCIP_VARTYPE_INTEGER:
      SCIPmessageFPrintInfo(file, "  [integer]");
      break;
   case SCIP_VARTYPE_IMPLINT:
      SCIPmessageFPrintInfo(file, "  [implicit]");
      break;
   case SCIP_VARTYPE_CONTINUOUS:
      SCIPmessageFPrintInfo(file, "  [continuous]");
      break;
   default:
      SCIPerrorMessage("unknown variable type\n");
      SCIPABORT();
   }

   /* name */
   SCIPmessageFPrintInfo(file, " <%s>:", var->name);

   /* objective value */
   SCIPmessageFPrintInfo(file, " obj=%g", var->obj);

   /* bounds (global bounds for transformed variables, original bounds for original variables) */
   if( SCIPvarIsTransformed(var) )
   {
      lb = SCIPvarGetLbGlobal(var);
      ub = SCIPvarGetUbGlobal(var);
   }
   else
   {
      lb = SCIPvarGetLbOriginal(var);
      ub = SCIPvarGetUbOriginal(var);
   }
   SCIPmessageFPrintInfo(file, ", bounds=");
   if( SCIPsetIsInfinity(set, lb) )
      SCIPmessageFPrintInfo(file, "[+inf,");
   else if( SCIPsetIsInfinity(set, -lb) )
      SCIPmessageFPrintInfo(file, "[-inf,");
   else
      SCIPmessageFPrintInfo(file, "[%g,", lb);
   if( SCIPsetIsInfinity(set, ub) )
      SCIPmessageFPrintInfo(file, "+inf]");
   else if( SCIPsetIsInfinity(set, -ub) )
      SCIPmessageFPrintInfo(file, "-inf]");
   else
      SCIPmessageFPrintInfo(file, "%g]", ub);

   /* holes */
   /**@todo print holes */

   /* fixings and aggregations */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      break;

   case SCIP_VARSTATUS_FIXED:
      SCIPmessageFPrintInfo(file, ", fixed:");
      if( SCIPsetIsInfinity(set, var->glbdom.lb) )
         SCIPmessageFPrintInfo(file, "+inf");
      else if( SCIPsetIsInfinity(set, -var->glbdom.lb) )
         SCIPmessageFPrintInfo(file, "-inf");
      else
         SCIPmessageFPrintInfo(file, "%g", var->glbdom.lb);
      break;

   case SCIP_VARSTATUS_AGGREGATED:
      SCIPmessageFPrintInfo(file, ", aggregated:");
      if( !SCIPsetIsZero(set, var->data.aggregate.constant) )
         SCIPmessageFPrintInfo(file, " %g", var->data.aggregate.constant);
      SCIPmessageFPrintInfo(file, " %+g<%s>", var->data.aggregate.scalar, SCIPvarGetName(var->data.aggregate.var));
      break;

   case SCIP_VARSTATUS_MULTAGGR:
      SCIPmessageFPrintInfo(file, ", aggregated:");
      if( !SCIPsetIsZero(set, var->data.multaggr.constant) )
         SCIPmessageFPrintInfo(file, " %g", var->data.multaggr.constant);
      for( i = 0; i < var->data.multaggr.nvars; ++i )
         SCIPmessageFPrintInfo(file, " %+g<%s>", var->data.multaggr.scalars[i], SCIPvarGetName(var->data.multaggr.vars[i]));
      break;

   case SCIP_VARSTATUS_NEGATED:
      SCIPmessageFPrintInfo(file, ", negated: %g - <%s>", var->data.negate.constant, SCIPvarGetName(var->negatedvar));
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
   }

   SCIPmessageFPrintInfo(file, "\n");
}

/** issues a VARUNLOCKED event on the given variable */
static
SCIP_RETCODE varEventVarUnlocked(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   )
{
   SCIP_EVENT* event;

   assert(var != NULL);
   assert(var->nlocksdown <= 1 && var->nlocksup <= 1);

   /* issue VARUNLOCKED event on variable */
   SCIP_CALL( SCIPeventCreateVarUnlocked(&event, blkmem, var) );
   SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, NULL, NULL, NULL, &event) );

   return SCIP_OKAY;
}

/** modifies lock numbers for rounding */
SCIP_RETCODE SCIPvarAddLocks(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   int                   addnlocksdown,      /**< increase in number of rounding down locks */
   int                   addnlocksup         /**< increase in number of rounding up locks */
   )
{
   int i;

   assert(var != NULL);
   assert(var->nlocksup >= 0);
   assert(var->nlocksdown >= 0);

   if( addnlocksdown == 0 && addnlocksup == 0 )
      return SCIP_OKAY;

   SCIPdebugMessage("add rounding locks %d/%d to variable <%s> (locks=%d/%d)\n",
      addnlocksdown, addnlocksup, var->name, var->nlocksdown, var->nlocksup);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
      {
         SCIP_CALL( SCIPvarAddLocks(var->data.original.transvar, blkmem, set, eventqueue, addnlocksdown, addnlocksup) );
      }
      else
      {
         var->nlocksdown += addnlocksdown;
         var->nlocksup += addnlocksup;
      }
      break;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_FIXED:
      var->nlocksdown += addnlocksdown;
      var->nlocksup += addnlocksup;
      if( var->nlocksdown <= 1 && var->nlocksup <= 1 )
      {
         SCIP_CALL( varEventVarUnlocked(var, blkmem, set, eventqueue) );
      }
      break;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
      {
         SCIP_CALL( SCIPvarAddLocks(var->data.aggregate.var, blkmem, set, eventqueue, addnlocksdown, addnlocksup) );
      }
      else
      {
         SCIP_CALL( SCIPvarAddLocks(var->data.aggregate.var, blkmem, set, eventqueue, addnlocksup, addnlocksdown) );
      }
      break;

   case SCIP_VARSTATUS_MULTAGGR:
      for( i = 0; i < var->data.multaggr.nvars; ++i )
      {
         if( var->data.multaggr.scalars[i] > 0.0 )
         {
            SCIP_CALL( SCIPvarAddLocks(var->data.multaggr.vars[i], blkmem, set, eventqueue, 
                  addnlocksdown, addnlocksup) );
         }
         else
         {
            SCIP_CALL( SCIPvarAddLocks(var->data.multaggr.vars[i], blkmem, set, eventqueue, 
                  addnlocksup, addnlocksdown) );
         }
      }
      break;

   case SCIP_VARSTATUS_NEGATED:
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarAddLocks(var->negatedvar, blkmem, set, eventqueue, addnlocksup, addnlocksdown) );
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   assert(var->nlocksdown >= 0);
   assert(var->nlocksup >= 0);

   return SCIP_OKAY;
}

/** gets number of locks for rounding down */
int SCIPvarGetNLocksDown(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   int nlocks;
   int i;

   assert(var != NULL);
   assert(var->nlocksdown >= 0);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
         return SCIPvarGetNLocksDown(var->data.original.transvar);
      else
         return var->nlocksdown;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_FIXED:
      return var->nlocksdown;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNLocksDown(var->data.aggregate.var);
      else
         return SCIPvarGetNLocksUp(var->data.aggregate.var);

   case SCIP_VARSTATUS_MULTAGGR:
      nlocks = 0;
      for( i = 0; i < var->data.multaggr.nvars; ++i )
      {
         if( var->data.multaggr.scalars[i] > 0.0 )
            nlocks += SCIPvarGetNLocksDown(var->data.multaggr.vars[i]);
         else
            nlocks += SCIPvarGetNLocksUp(var->data.multaggr.vars[i]);
      }
      return nlocks;

   case SCIP_VARSTATUS_NEGATED:
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return SCIPvarGetNLocksUp(var->negatedvar);

   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** gets number of locks for rounding up */
int SCIPvarGetNLocksUp(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   int nlocks;
   int i;

   assert(var != NULL);
   assert(var->nlocksup >= 0);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
         return SCIPvarGetNLocksUp(var->data.original.transvar);
      else
         return var->nlocksup;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_FIXED:
      return var->nlocksup;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNLocksUp(var->data.aggregate.var);
      else
         return SCIPvarGetNLocksDown(var->data.aggregate.var);

   case SCIP_VARSTATUS_MULTAGGR:
      nlocks = 0;
      for( i = 0; i < var->data.multaggr.nvars; ++i )
      {
         if( var->data.multaggr.scalars[i] > 0.0 )
            nlocks += SCIPvarGetNLocksUp(var->data.multaggr.vars[i]);
         else
            nlocks += SCIPvarGetNLocksDown(var->data.multaggr.vars[i]);
      }
      return nlocks;

   case SCIP_VARSTATUS_NEGATED:
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return SCIPvarGetNLocksDown(var->negatedvar);

   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** is it possible, to round variable down and stay feasible? */
SCIP_Bool SCIPvarMayRoundDown(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   return (SCIPvarGetNLocksDown(var) == 0);
}

/** is it possible, to round variable up and stay feasible? */
SCIP_Bool SCIPvarMayRoundUp(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   return (SCIPvarGetNLocksUp(var) == 0);
}

/** gets and captures transformed variable of a given variable; if the variable is not yet transformed,
 *  a new transformed variable for this variable is created
 */
SCIP_RETCODE SCIPvarTransform(
   SCIP_VAR*             origvar,            /**< original problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_OBJSENSE         objsense,           /**< objective sense of original problem; transformed is always MINIMIZE */
   SCIP_VAR**            transvar            /**< pointer to store the transformed variable */
   )
{
   char name[SCIP_MAXSTRLEN];

   assert(origvar != NULL);
   assert(SCIPvarGetStatus(origvar) == SCIP_VARSTATUS_ORIGINAL);
   assert(origvar->glbdom.lb == origvar->locdom.lb); /*lint !e777*/
   assert(origvar->glbdom.ub == origvar->locdom.ub); /*lint !e777*/
   assert(origvar->vlbs == NULL);
   assert(origvar->vubs == NULL);
   assert(transvar != NULL);

   /* check if variable is already transformed */
   if( origvar->data.original.transvar != NULL )
   {
      *transvar = origvar->data.original.transvar;
      SCIPvarCapture(*transvar);
   }
   else
   {
      /* create transformed variable */
      sprintf(name, "t_%s", origvar->name);
      SCIP_CALL( SCIPvarCreateTransformed(transvar, blkmem, set, stat, name,
            origvar->glbdom.lb, origvar->glbdom.ub, (SCIP_Real)objsense * origvar->obj,
            SCIPvarGetType(origvar), origvar->initial, origvar->removeable,
            NULL, NULL, origvar->vardeltrans, NULL) );
      
      /* copy the branch factor and priority */
      (*transvar)->branchfactor = origvar->branchfactor;
      (*transvar)->branchpriority = origvar->branchpriority;
      (*transvar)->branchdirection = origvar->branchdirection;

      /* duplicate hole lists */
      SCIP_CALL( holelistDuplicate(&(*transvar)->glbdom.holelist, blkmem, set, origvar->glbdom.holelist) );
      SCIP_CALL( holelistDuplicate(&(*transvar)->locdom.holelist, blkmem, set, origvar->locdom.holelist) );
      
      /* link original and transformed variable */
      origvar->data.original.transvar = *transvar;
      SCIP_CALL( varAddParent(*transvar, blkmem, set, origvar) );
      
      /* copy rounding locks */
      (*transvar)->nlocksdown = origvar->nlocksdown;
      (*transvar)->nlocksup = origvar->nlocksup;
      assert((*transvar)->nlocksdown >= 0);
      assert((*transvar)->nlocksup >= 0);

      /* transform user data */
      if( origvar->vartrans != NULL )
      {
         SCIP_CALL( origvar->vartrans(set->scip, origvar, origvar->vardata, *transvar, &(*transvar)->vardata) );
      }
      else
         (*transvar)->vardata = origvar->vardata;
   }

   SCIPdebugMessage("transformed variable: <%s>[%p] -> <%s>[%p]\n", origvar->name, origvar, (*transvar)->name, *transvar);

   return SCIP_OKAY;
}

/** gets corresponding transformed variable of an original or negated original variable */
SCIP_RETCODE SCIPvarGetTransformed(
   SCIP_VAR*             origvar,            /**< original problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_VAR**            transvar            /**< pointer to store the transformed variable, or NULL if not existing yet */
   )
{
   assert(origvar != NULL);
   assert(SCIPvarGetStatus(origvar) == SCIP_VARSTATUS_ORIGINAL || SCIPvarGetStatus(origvar) == SCIP_VARSTATUS_NEGATED);

   if( SCIPvarGetStatus(origvar) == SCIP_VARSTATUS_NEGATED )
   {
      assert(origvar->negatedvar != NULL);
      assert(SCIPvarGetStatus(origvar->negatedvar) == SCIP_VARSTATUS_ORIGINAL);

      if( origvar->negatedvar->data.original.transvar == NULL )
         *transvar = NULL;
      else
      {
         SCIP_CALL( SCIPvarNegate(origvar->negatedvar->data.original.transvar, blkmem, set, stat, transvar) );
      }
   }
   else
      *transvar = origvar->data.original.transvar;

   return SCIP_OKAY;
}

/** converts loose transformed variable into column variable, creates LP column */
SCIP_RETCODE SCIPvarColumn(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_LP*              lp                  /**< current LP data */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE);

   SCIPdebugMessage("creating column for variable <%s>\n", var->name);

   /* switch variable status */
   var->varstatus = SCIP_VARSTATUS_COLUMN; /*lint !e641*/

   /* create column of variable */
   SCIP_CALL( SCIPcolCreate(&var->data.col, blkmem, set, stat, var, 0, NULL, NULL, var->removeable) );

   if( var->probindex != -1 )
   {
      /* inform problem about the variable's status change */
      SCIP_CALL( SCIPprobVarChangedStatus(prob, set, NULL, var) );
      
      /* inform LP, that problem variable is now a column variable and no longer loose */
      SCIP_CALL( SCIPlpUpdateVarColumn(lp, set, var) );
   }

   return SCIP_OKAY;
}

/** converts column transformed variable back into loose variable, frees LP column */
SCIP_RETCODE SCIPvarLoose(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_LP*              lp                  /**< current LP data */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);
   assert(var->data.col != NULL);
   assert(var->data.col->len == var->data.col->nunlinked); /* no rows can include the column */
   assert(var->data.col->lppos == -1);
   assert(var->data.col->lpipos == -1);

   SCIPdebugMessage("deleting column for variable <%s>\n", var->name);

   /* free column of variable */
   SCIP_CALL( SCIPcolFree(&var->data.col, blkmem, set, lp) );

   /* switch variable status */
   var->varstatus = SCIP_VARSTATUS_LOOSE; /*lint !e641*/

   if( var->probindex != -1 )
   {
      /* inform problem about the variable's status change */
      SCIP_CALL( SCIPprobVarChangedStatus(prob, set, NULL, var) );
      
      /* inform LP, that problem variable is now a loose variable and no longer a column */
      SCIP_CALL( SCIPlpUpdateVarLoose(lp, set, var) );
   }

   return SCIP_OKAY;
}

/** issues a VARFIXED event on the given variable and all its parents (except ORIGINAL parents);
 *  the event issuing on the parents is necessary, because unlike with bound changes, the parent variables
 *  are not informed about a fixing of an active variable they are pointing to
 */
static
SCIP_RETCODE varEventVarFixed(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   )
{
   SCIP_EVENT* event;
   int i;

   assert(var != NULL);

   /* issue VARFIXED event on variable */
   SCIP_CALL( SCIPeventCreateVarFixed(&event, blkmem, var) );
   SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, NULL, NULL, NULL, &event) );

   /* process all parents */
   for( i = 0; i < var->nparentvars; ++i )
   {
      if( SCIPvarGetStatus(var->parentvars[i]) != SCIP_VARSTATUS_ORIGINAL )
      {
         SCIP_CALL( varEventVarFixed(var->parentvars[i], blkmem, set, eventqueue) );
      }
   }

   return SCIP_OKAY;
}

/** converts variable into fixed variable */
SCIP_RETCODE SCIPvarFix(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             fixedval,           /**< value to fix variable at */
   SCIP_Bool*            infeasible,         /**< pointer to store whether the fixing is infeasible */
   SCIP_Bool*            fixed               /**< pointer to store whether the fixing was performed (variable was unfixed) */
   )
{
   SCIP_Real obj;

   assert(var != NULL);
   assert(var->glbdom.lb == var->locdom.lb); /*lint !e777*/
   assert(var->glbdom.ub == var->locdom.ub); /*lint !e777*/
   assert(SCIPsetIsLE(set, var->glbdom.lb, fixedval));
   assert(SCIPsetIsLE(set, fixedval, var->glbdom.ub));
   assert(infeasible != NULL);
   assert(fixed != NULL);

   SCIPdebugMessage("fix variable <%s>[%g,%g] to %g\n", var->name, var->glbdom.lb, var->glbdom.ub, fixedval);

   *infeasible = FALSE;
   *fixed = FALSE;

   if( (SCIPvarGetType(var) != SCIP_VARTYPE_CONTINUOUS && !SCIPsetIsFeasIntegral(set, fixedval))
      || SCIPsetIsFeasLT(set, fixedval, var->locdom.lb)
      || SCIPsetIsFeasGT(set, fixedval, var->locdom.ub) )
   {
      SCIPdebugMessage(" -> fixing infeasible: locdom=[%g,%g], fixedval=%g\n", var->locdom.lb, var->locdom.ub, fixedval);
      *infeasible = TRUE;
      return SCIP_OKAY;
   }
   else if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_FIXED )
   {
      *infeasible = !SCIPsetIsFeasEQ(set, fixedval, var->locdom.lb);
      SCIPdebugMessage(" -> variable already fixed to %g (fixedval=%g): infeasible=%d\n", 
         var->locdom.lb, fixedval, *infeasible);
      return SCIP_OKAY;
   }

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot fix an untransformed original variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarFix(var->data.original.transvar, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, 
            fixedval, infeasible, fixed) );
      break;

   case SCIP_VARSTATUS_LOOSE:
      assert(!SCIPeventqueueIsDelayed(eventqueue)); /* otherwise, the pseudo objective value update gets confused */

      /* set the fixed variable's objective value to 0.0 */
      obj = var->obj;
      SCIP_CALL( SCIPvarChgObj(var, blkmem, set, primal, lp, eventqueue, 0.0) );

      /* change variable's bounds to fixed value (thereby removing redundant implications and variable bounds) */
      holelistFree(&var->glbdom.holelist, blkmem);
      holelistFree(&var->locdom.holelist, blkmem);
      SCIP_CALL( SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, fixedval) );
      SCIP_CALL( SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, fixedval) );

      /* explicitly set variable's bounds, even if the fixed value is in epsilon range of the old bound */
      var->glbdom.lb = fixedval;
      var->glbdom.ub = fixedval;
      var->locdom.lb = fixedval;
      var->locdom.ub = fixedval;

      /* delete implications and variable bounds information */
      SCIP_CALL( varRemoveImplicsVbs(var, blkmem, set, FALSE, TRUE) );
      assert(var->vlbs == NULL);
      assert(var->vubs == NULL);
      assert(var->implics == NULL);

      /* remove the variable from all cliques */
      if( SCIPvarGetType(var) == SCIP_VARTYPE_BINARY )
      {
         SCIPcliquelistRemoveFromCliques(var->cliquelist, var);
         SCIPcliquelistFree(&var->cliquelist, blkmem);
      }
      assert(var->cliquelist == NULL);

      /* clear the history of the variable */
      SCIPhistoryReset(var->history);
      SCIPhistoryReset(var->historycrun);

      /* convert variable into fixed variable */
      var->varstatus = SCIP_VARSTATUS_FIXED; /*lint !e641*/

      /* inform problem about the variable's status change */
      if( var->probindex != -1 )
      {
         SCIP_CALL( SCIPprobVarChangedStatus(prob, set, branchcand, var) );
      }

      /* reset the objective value of the fixed variable, thus adjusting the problem's objective offset */
      SCIP_CALL( SCIPvarAddObj(var, blkmem, set, stat, prob, primal, tree, lp, eventqueue, obj) );

      /* issue VARFIXED event */
      SCIP_CALL( varEventVarFixed(var, blkmem, set, eventqueue) );

      *fixed = TRUE;
      break;

   case SCIP_VARSTATUS_COLUMN:
      SCIPerrorMessage("cannot fix a column variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot fix a fixed variable again\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      /* fix aggregation variable y in x = a*y + c, instead of fixing x directly */
      assert(SCIPsetIsZero(set, var->obj));
      assert(!SCIPsetIsZero(set, var->data.aggregate.scalar));
      SCIP_CALL( SCIPvarFix(var->data.aggregate.var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue,
            (fixedval - var->data.aggregate.constant)/var->data.aggregate.scalar, infeasible, fixed) );
      break;

   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot fix a multiple aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      /* fix negation variable x in x' = offset - x, instead of fixing x' directly */
      assert(SCIPsetIsZero(set, var->obj));
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarFix(var->negatedvar, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue,
            var->data.negate.constant - fixedval, infeasible, fixed) );
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
   
   return SCIP_OKAY;
}

/** tightens the bounds of both variables in aggregation x = a*y + c */
static
SCIP_RETCODE varUpdateAggregationBounds(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             aggvar,             /**< variable y in aggregation x = a*y + c */
   SCIP_Real             scalar,             /**< multiplier a in aggregation x = a*y + c */
   SCIP_Real             constant,           /**< constant shift c in aggregation x = a*y + c */
   SCIP_Bool*            infeasible,         /**< pointer to store whether the aggregation is infeasible */
   SCIP_Bool*            fixed               /**< pointer to store whether the variables were fixed */
   )
{
   SCIP_Real varlb;
   SCIP_Real varub;
   SCIP_Real aggvarlb;
   SCIP_Real aggvarub;
   SCIP_Bool aggvarbdschanged;

   assert(var != NULL);
   assert(aggvar != NULL);
   assert(!SCIPsetIsZero(set, scalar));
   assert(infeasible != NULL);
   assert(fixed != NULL);

   *infeasible = FALSE;
   *fixed = FALSE;

   SCIPdebugMessage("updating bounds of variables in aggregation <%s> == %g*<%s> %+g\n",
      var->name, scalar, aggvar->name, constant);
   SCIPdebugMessage("  old bounds: <%s> [%g,%g]   <%s> [%g,%g]\n",
      var->name, var->glbdom.lb, var->glbdom.ub, aggvar->name, aggvar->glbdom.lb, aggvar->glbdom.ub);

   /* loop as long additional changes may be found */
   do
   {
      aggvarbdschanged = FALSE;

      /* update the bounds of the aggregated variable x in x = a*y + c */
      if( scalar > 0.0 )
      {
         if( SCIPsetIsInfinity(set, -aggvar->glbdom.lb) )
            varlb = -SCIPsetInfinity(set);
         else
            varlb = aggvar->glbdom.lb * scalar + constant;
         if( SCIPsetIsInfinity(set, aggvar->glbdom.ub) )
            varub = SCIPsetInfinity(set);
         else
            varub = aggvar->glbdom.ub * scalar + constant;
      }
      else
      {
         if( SCIPsetIsInfinity(set, -aggvar->glbdom.lb) )
            varub = SCIPsetInfinity(set);
         else
            varub = aggvar->glbdom.lb * scalar + constant;
         if( SCIPsetIsInfinity(set, aggvar->glbdom.ub) )
            varlb = -SCIPsetInfinity(set);
         else
            varlb = aggvar->glbdom.ub * scalar + constant;
      }
      varlb = MAX(varlb, var->glbdom.lb);
      varub = MIN(varub, var->glbdom.ub);
      SCIPvarAdjustLb(var, set, &varlb);
      SCIPvarAdjustUb(var, set, &varub);

      /* check the new bounds */
      if( SCIPsetIsGT(set, varlb, varub) )
      {
         /* the aggregation is infeasible */
         *infeasible = TRUE;
         return SCIP_OKAY;
      }
      else if( SCIPsetIsEQ(set, varlb, varub) )
      {
         /* the aggregated variable is fixed -> fix both variables */
         SCIP_CALL( SCIPvarFix(var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, 
               varlb, infeasible, fixed) );
         if( !(*infeasible) )
         {
            SCIP_Bool aggfixed;

            SCIP_CALL( SCIPvarFix(aggvar, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, 
                  (varlb-constant)/scalar, infeasible, &aggfixed) );
            assert(*fixed == aggfixed);
         }
         return SCIP_OKAY;
      }
      else
      {
         if( SCIPsetIsGT(set, varlb, var->glbdom.lb) )
         {
            SCIP_CALL( SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, varlb) );
         }
         if( SCIPsetIsLT(set, varub, var->glbdom.ub) )
         {
            SCIP_CALL( SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, varub) );
         }

         /* update the hole list of the aggregation variable */
         /**@todo update hole list of aggregation variable */
      }

      /* update the bounds of the aggregation variable y in x = a*y + c  ->  y = (x-c)/a */
      if( scalar > 0.0 )
      {
         if( SCIPsetIsInfinity(set, -var->glbdom.lb) )
            aggvarlb = -SCIPsetInfinity(set);
         else
            aggvarlb = (var->glbdom.lb - constant) / scalar;
         if( SCIPsetIsInfinity(set, var->glbdom.ub) )
            aggvarub = SCIPsetInfinity(set);
         else
            aggvarub = (var->glbdom.ub - constant) / scalar;
      }
      else
      {
         if( SCIPsetIsInfinity(set, -var->glbdom.lb) )
            aggvarub = SCIPsetInfinity(set);
         else
            aggvarub = (var->glbdom.lb - constant) / scalar;
         if( SCIPsetIsInfinity(set, var->glbdom.ub) )
            aggvarlb = -SCIPsetInfinity(set);
         else
            aggvarlb = (var->glbdom.ub - constant) / scalar;
      }
      aggvarlb = MAX(aggvarlb, aggvar->glbdom.lb);
      aggvarub = MIN(aggvarub, aggvar->glbdom.ub);
      SCIPvarAdjustLb(aggvar, set, &aggvarlb);
      SCIPvarAdjustUb(aggvar, set, &aggvarub);

      /* check the new bounds */
      if( SCIPsetIsGT(set, aggvarlb, aggvarub) )
      {
         /* the aggregation is infeasible */
         *infeasible = TRUE;
         return SCIP_OKAY;
      }
      else if( SCIPsetIsEQ(set, aggvarlb, aggvarub) )
      {
         /* the aggregation variable is fixed -> fix both variables */
         SCIP_CALL( SCIPvarFix(aggvar, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, aggvarlb, 
               infeasible, fixed) );
         if( !(*infeasible) )
         {
            SCIP_Bool varfixed;

            SCIP_CALL( SCIPvarFix(var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, 
                  aggvarlb * scalar + constant, infeasible, &varfixed) );
            assert(*fixed == varfixed);
         }
         return SCIP_OKAY;
      }
      else
      {
         if( SCIPsetIsGT(set, aggvarlb, aggvar->glbdom.lb) )
         {
            SCIP_CALL( SCIPvarChgLbGlobal(aggvar, blkmem, set, stat, lp, branchcand, eventqueue, aggvarlb) );
            aggvarbdschanged = TRUE;
         }
         if( SCIPsetIsLT(set, aggvarub, aggvar->glbdom.ub) )
         {
            SCIP_CALL( SCIPvarChgUbGlobal(aggvar, blkmem, set, stat, lp, branchcand, eventqueue, aggvarub) );
            aggvarbdschanged = TRUE;
         }

         /* update the hole list of the aggregation variable */
         /**@todo update hole list of aggregation variable */
      }
   }
   while( aggvarbdschanged );

   SCIPdebugMessage("  new bounds: <%s> [%g,%g]   <%s> [%g,%g]\n",
      var->name, var->glbdom.lb, var->glbdom.ub, aggvar->name, aggvar->glbdom.lb, aggvar->glbdom.ub);

   return SCIP_OKAY;
}

/** converts loose variable into aggregated variable */
SCIP_RETCODE SCIPvarAggregate(
   SCIP_VAR*             var,                /**< loose problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             aggvar,             /**< loose variable y in aggregation x = a*y + c */
   SCIP_Real             scalar,             /**< multiplier a in aggregation x = a*y + c */
   SCIP_Real             constant,           /**< constant shift c in aggregation x = a*y + c */
   SCIP_Bool*            infeasible,         /**< pointer to store whether the aggregation is infeasible */
   SCIP_Bool*            aggregated          /**< pointer to store whether the aggregation was successful */
   )
{
   SCIP_VAR** vars;
   SCIP_Real* coefs;
   SCIP_Real* constants;
   SCIP_Real obj;
   SCIP_Real branchfactor;
   SCIP_Bool fixed;
   int branchpriority;
   int nlocksdown;
   int nlocksup;
   int nvbds;
   int i;
   int j;

   assert(var != NULL);
   assert(var->glbdom.lb == var->locdom.lb); /*lint !e777*/
   assert(var->glbdom.ub == var->locdom.ub); /*lint !e777*/
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE);
   assert(!SCIPeventqueueIsDelayed(eventqueue)); /* otherwise, the pseudo objective value update gets confused */
   assert(infeasible != NULL);
   assert(aggregated != NULL);

   *infeasible = FALSE;
   *aggregated = FALSE;

   /* get active problem variable of aggregation variable */
   SCIP_CALL( SCIPvarGetProbvarSum(&aggvar, &scalar, &constant) );

   /* aggregation is a fixing, if the scalar is zero */
   if( SCIPsetIsZero(set, scalar) )
      return SCIPvarFix(var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, constant,
         infeasible, aggregated);

   /* don't perform the aggregation if the aggregation variable is multi-aggregated itself */
   if( SCIPvarGetStatus(aggvar) == SCIP_VARSTATUS_MULTAGGR )
      return SCIP_OKAY;

   assert(aggvar != NULL);
   assert(aggvar->glbdom.lb == aggvar->locdom.lb); /*lint !e777*/
   assert(aggvar->glbdom.ub == aggvar->locdom.ub); /*lint !e777*/
   assert(SCIPvarGetStatus(aggvar) == SCIP_VARSTATUS_LOOSE);

   SCIPdebugMessage("aggregate variable <%s>[%g,%g] == %g*<%s>[%g,%g] %+g\n", var->name, var->glbdom.lb, var->glbdom.ub,
      scalar, aggvar->name, aggvar->glbdom.lb, aggvar->glbdom.ub, constant);

   /* if variable and aggregation variable are equal, the variable can be fixed: x == a*x + c  =>  x == c/(1-a) */
   if( var == aggvar )
   {
      if( SCIPsetIsEQ(set, scalar, 1.0) )
         *infeasible = !SCIPsetIsZero(set, constant);
      else
      {
         SCIP_CALL( SCIPvarFix(var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue,
               constant/(1.0-scalar), infeasible, aggregated) );
      }
      return SCIP_OKAY;
   }

   /* tighten the bounds of aggregated and aggregation variable */
   SCIP_CALL( varUpdateAggregationBounds(var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue,
         aggvar, scalar, constant, infeasible, &fixed) );
   if( *infeasible || fixed )
   {
      *aggregated = fixed;
      return SCIP_OKAY;
   }

   /* delete implications and variable bounds of the aggregated variable from other variables, but keep them in the
    * aggregated variable
    */
   SCIP_CALL( varRemoveImplicsVbs(var, blkmem, set, FALSE, FALSE) );

   /* set the aggregated variable's objective value to 0.0 */
   obj = var->obj;
   SCIP_CALL( SCIPvarChgObj(var, blkmem, set, primal, lp, eventqueue, 0.0) );

   /* unlock all rounding locks */
   nlocksdown = var->nlocksdown;
   nlocksup = var->nlocksup;
   var->nlocksdown = 0;
   var->nlocksup = 0;

   /* check, if variable should be used as NEGATED variable of the aggregation variable */
   if( SCIPvarGetType(var) == SCIP_VARTYPE_BINARY && SCIPvarGetType(aggvar) == SCIP_VARTYPE_BINARY
      && var->negatedvar == NULL && aggvar->negatedvar == NULL
      && SCIPsetIsEQ(set, scalar, -1.0) && SCIPsetIsEQ(set, constant, 1.0) )
   {
      /* link both variables as negation pair */
      var->varstatus = SCIP_VARSTATUS_NEGATED; /*lint !e641*/
      var->data.negate.constant = 1.0;
      var->negatedvar = aggvar;
      aggvar->negatedvar = var;
   }
   else
   {
      /* convert variable into aggregated variable */
      var->varstatus = SCIP_VARSTATUS_AGGREGATED; /*lint !e641*/
      var->data.aggregate.var = aggvar;
      var->data.aggregate.scalar = scalar;
      var->data.aggregate.constant = constant;
   }

   /* make aggregated variable a parent of the aggregation variable */
   SCIP_CALL( varAddParent(aggvar, blkmem, set, var) );

   /* relock the rounding locks of the variable, thus increasing the locks of the aggregation variable */
   SCIP_CALL( SCIPvarAddLocks(var, blkmem, set, eventqueue, nlocksdown, nlocksup) );

   /* move the variable bounds to the aggregation variable:
    *  - add all variable bounds again to the variable, thus adding it to the aggregation variable
    *  - free the variable bounds data structures
    */
   if( var->vlbs != NULL )
   {
      nvbds = SCIPvboundsGetNVbds(var->vlbs);
      vars = SCIPvboundsGetVars(var->vlbs);
      coefs = SCIPvboundsGetCoefs(var->vlbs);
      constants = SCIPvboundsGetConstants(var->vlbs);
      for( i = 0; i < nvbds && !(*infeasible); ++i )
      {
         SCIP_CALL( SCIPvarAddVlb(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
               vars[i], coefs[i], constants[i], TRUE, infeasible, NULL) );
      }
   }
   if( var->vubs != NULL )
   {
      nvbds = SCIPvboundsGetNVbds(var->vubs);
      vars = SCIPvboundsGetVars(var->vubs);
      coefs = SCIPvboundsGetCoefs(var->vubs);
      constants = SCIPvboundsGetConstants(var->vubs);
      for( i = 0; i < nvbds && !(*infeasible); ++i )
      {
         SCIP_CALL( SCIPvarAddVub(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
               vars[i], coefs[i], constants[i], TRUE, infeasible, NULL) );
      }
   }
   SCIPvboundsFree(&var->vlbs, blkmem);
   SCIPvboundsFree(&var->vubs, blkmem);

   /* move the implications to the aggregation variable:
    *  - add all implications again to the variable, thus adding it to the aggregation variable
    *  - free the implications data structures
    */
   if( var->implics != NULL && SCIPvarGetType(aggvar) == SCIP_VARTYPE_BINARY )
   {
      assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);
      for( i = 0; i < 2; ++i )
      {
         SCIP_VAR** implvars;
         SCIP_BOUNDTYPE* impltypes;
         SCIP_Real* implbounds;
         int nimpls;

         nimpls = SCIPimplicsGetNImpls(var->implics, (SCIP_Bool)i);
         implvars = SCIPimplicsGetVars(var->implics, (SCIP_Bool)i);
         impltypes = SCIPimplicsGetTypes(var->implics, (SCIP_Bool)i);
         implbounds = SCIPimplicsGetBounds(var->implics, (SCIP_Bool)i);

         for( j = 0; j < nimpls && !(*infeasible); ++j )
         {
            SCIP_CALL( SCIPvarAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
                  (SCIP_Bool)i, implvars[j], impltypes[j], implbounds[j], TRUE, infeasible, NULL) );
         }
      }
   }
   SCIPimplicsFree(&var->implics, blkmem);

   /* move the cliques to the aggregation variable:
    *  - remove the variable from all cliques it is contained in
    *  - add all cliques again to the variable, thus adding it to the aggregated variable
    *  - free the cliquelist data structures
    */
   if( SCIPvarGetType(var) == SCIP_VARTYPE_BINARY )
   {
      SCIPcliquelistRemoveFromCliques(var->cliquelist, var);
      if( var->cliquelist != NULL && SCIPvarGetType(aggvar) == SCIP_VARTYPE_BINARY )
      {
         for( i = 0; i < 2; ++i )
         {
            SCIP_CLIQUE** cliques;
            int ncliques;
            
            ncliques = SCIPcliquelistGetNCliques(var->cliquelist, (SCIP_Bool)i);
            cliques = SCIPcliquelistGetCliques(var->cliquelist, (SCIP_Bool)i);
            for( j = 0; j < ncliques && !(*infeasible); ++j )
            {
               SCIP_CALL( SCIPvarAddClique(var, blkmem, set, stat, lp, branchcand, eventqueue,
                     (SCIP_Bool)i, cliques[j], infeasible, NULL) );
            }
         }
      }
      SCIPcliquelistFree(&var->cliquelist, blkmem);
   }
   assert(var->cliquelist == NULL);

   /* add the history entries to the aggregation variable and clear the history of the aggregated variable */
   SCIPhistoryUnite(aggvar->history, var->history, scalar < 0.0);
   SCIPhistoryUnite(aggvar->historycrun, var->historycrun, scalar < 0.0);
   SCIPhistoryReset(var->history);
   SCIPhistoryReset(var->historycrun);

   /* update flags of aggregation variable */
   aggvar->removeable &= var->removeable;

   /* update branching factors and priorities of both variables to be the maximum of both variables */
   branchfactor = MAX(aggvar->branchfactor, var->branchfactor);
   branchpriority = MAX(aggvar->branchpriority, var->branchpriority);
   SCIPvarChgBranchFactor(aggvar, set, branchfactor);
   SCIPvarChgBranchPriority(aggvar, branchpriority);
   SCIPvarChgBranchFactor(var, set, branchfactor);
   SCIPvarChgBranchPriority(var, branchpriority);

   /* update branching direction of both variables to agree to a single direction */
   if( scalar >= 0.0 )
   {
      if( (SCIP_BRANCHDIR)var->branchdirection == SCIP_BRANCHDIR_AUTO )
         SCIPvarChgBranchDirection(var, (SCIP_BRANCHDIR)aggvar->branchdirection);
      else if( (SCIP_BRANCHDIR)aggvar->branchdirection == SCIP_BRANCHDIR_AUTO )
         SCIPvarChgBranchDirection(aggvar, (SCIP_BRANCHDIR)var->branchdirection);
      else if( var->branchdirection != aggvar->branchdirection )
         SCIPvarChgBranchDirection(var, SCIP_BRANCHDIR_AUTO);
   }
   else
   {
      if( (SCIP_BRANCHDIR)var->branchdirection == SCIP_BRANCHDIR_AUTO )
         SCIPvarChgBranchDirection(var, SCIPbranchdirOpposite((SCIP_BRANCHDIR)aggvar->branchdirection));
      else if( (SCIP_BRANCHDIR)aggvar->branchdirection == SCIP_BRANCHDIR_AUTO )
         SCIPvarChgBranchDirection(aggvar, SCIPbranchdirOpposite((SCIP_BRANCHDIR)var->branchdirection));
      else if( var->branchdirection != aggvar->branchdirection )
         SCIPvarChgBranchDirection(var, SCIP_BRANCHDIR_AUTO);
   }

   if( var->probindex != -1 )
   {
      /* inform problem about the variable's status change */
      SCIP_CALL( SCIPprobVarChangedStatus(prob, set, branchcand, var) );
   }

   /* reset the objective value of the aggregated variable, thus adjusting the objective value of the aggregation
    * variable and the problem's objective offset
    */
   SCIP_CALL( SCIPvarAddObj(var, blkmem, set, stat, prob, primal, tree, lp, eventqueue, obj) );

   /* issue VARFIXED event */
   SCIP_CALL( varEventVarFixed(var, blkmem, set, eventqueue) );

   *aggregated = TRUE;

   return SCIP_OKAY;
}

/** converts variable into multi-aggregated variable */
SCIP_RETCODE SCIPvarMultiaggregate(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   int                   naggvars,           /**< number n of variables in aggregation x = a_1*y_1 + ... + a_n*y_n + c */
   SCIP_VAR**            aggvars,            /**< variables y_i in aggregation x = a_1*y_1 + ... + a_n*y_n + c */
   SCIP_Real*            scalars,            /**< multipliers a_i in aggregation x = a_1*y_1 + ... + a_n*y_n + c */
   SCIP_Real             constant,           /**< constant shift c in aggregation x = a_1*y_1 + ... + a_n*y_n + c */
   SCIP_Bool*            infeasible,         /**< pointer to store whether the aggregation is infeasible */
   SCIP_Bool*            aggregated          /**< pointer to store whether the aggregation was successful */
   )
{
   SCIP_EVENT* event;
   SCIP_Real obj;
   SCIP_Real branchfactor;
   int branchpriority;
   SCIP_BRANCHDIR branchdirection;
   int nlocksdown;
   int nlocksup;
   int v;

   assert(var != NULL);
   assert(var->glbdom.lb == var->locdom.lb); /*lint !e777*/
   assert(var->glbdom.ub == var->locdom.ub); /*lint !e777*/
   assert(naggvars == 0 || aggvars != NULL);
   assert(naggvars == 0 || scalars != NULL);
   assert(infeasible != NULL);
   assert(aggregated != NULL);

   /* check, if we are in one of the simple cases */
   if( naggvars == 0 )
      return SCIPvarFix(var, blkmem, set, stat, prob, primal, tree, lp, branchcand, eventqueue, constant, 
         infeasible, aggregated);
   else if( naggvars == 1)
      return SCIPvarAggregate(var, blkmem, set, stat, prob, primal, tree, lp, cliquetable, branchcand, eventqueue, 
         aggvars[0], scalars[0], constant, infeasible, aggregated);

   SCIPdebugMessage("multi-aggregate variable <%s> == ...%d vars... %+g\n", var->name, naggvars, constant);

   *infeasible = FALSE;
   *aggregated = FALSE;

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot multi-aggregate an untransformed original variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarMultiaggregate(var->data.original.transvar, blkmem, set, stat, prob, primal, tree, lp,
            cliquetable, branchcand, eventqueue, naggvars, aggvars, scalars, constant, infeasible, aggregated) );
      break;

   case SCIP_VARSTATUS_LOOSE:
      assert(!SCIPeventqueueIsDelayed(eventqueue)); /* otherwise, the pseudo objective value update gets confused */

      /* if the variable to be multiaggregated has implications or variable bounds (i.e. is the implied variable or
       * variable bound variable of another variable), we have to remove it from the other variables implications or
       * variable bounds
       */
      SCIP_CALL( varRemoveImplicsVbs(var, blkmem, set, FALSE, TRUE) );
      assert(var->vlbs == NULL);
      assert(var->vubs == NULL);
      assert(var->implics == NULL);

      /* the variable also has to be removed from all cliques */
      if( SCIPvarGetType(var) == SCIP_VARTYPE_BINARY )
      {
         SCIPcliquelistRemoveFromCliques(var->cliquelist, var);
         SCIPcliquelistFree(&var->cliquelist, blkmem);
      }
      assert(var->cliquelist == NULL);

      /* set the aggregated variable's objective value to 0.0 */
      obj = var->obj;
      SCIP_CALL( SCIPvarChgObj(var, blkmem, set, primal, lp, eventqueue, 0.0) );

      /* unlock all rounding locks */
      nlocksdown = var->nlocksdown;
      nlocksup = var->nlocksup;
      var->nlocksdown = 0;
      var->nlocksup = 0;

      /* convert variable into multi-aggregated variable */
      var->varstatus = SCIP_VARSTATUS_MULTAGGR; /*lint !e641*/
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &var->data.multaggr.vars, aggvars, naggvars) );
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &var->data.multaggr.scalars, scalars, naggvars) );
      var->data.multaggr.constant = constant;
      var->data.multaggr.nvars = naggvars;
      var->data.multaggr.varssize = naggvars;

      /* relock the rounding locks of the variable, thus increasing the locks of the aggregation variables */
      SCIP_CALL( SCIPvarAddLocks(var, blkmem, set, eventqueue, nlocksdown, nlocksup) );

      /* update flags and branching factors and priorities of aggregation variables;
       * update preferred branching direction of all aggregation variables that don't have a preferred direction yet
       */
      branchfactor = var->branchfactor;
      branchpriority = var->branchpriority;
      branchdirection = (SCIP_BRANCHDIR)var->branchdirection;
      for( v = 0; v < naggvars; ++v )
      {
         assert(aggvars[v] != NULL);
         aggvars[v]->removeable &= var->removeable;
         branchfactor = MAX(aggvars[v]->branchfactor, branchfactor);
         branchpriority = MAX(aggvars[v]->branchpriority, branchpriority);
      }
      for( v = 0; v < naggvars; ++v )
      {
         SCIPvarChgBranchFactor(aggvars[v], set, branchfactor);
         SCIPvarChgBranchPriority(aggvars[v], branchpriority);
         if( (SCIP_BRANCHDIR)aggvars[v]->branchdirection == SCIP_BRANCHDIR_AUTO )
         {
            if( scalars[v] >= 0.0 )
               SCIPvarChgBranchDirection(aggvars[v], branchdirection);
            else
               SCIPvarChgBranchDirection(aggvars[v], SCIPbranchdirOpposite(branchdirection));
         }
      }
      SCIPvarChgBranchFactor(var, set, branchfactor);
      SCIPvarChgBranchPriority(var, branchpriority);

      if( var->probindex != -1 )
      {
         /* inform problem about the variable's status change */
         SCIP_CALL( SCIPprobVarChangedStatus(prob, set, branchcand, var) );
      }

      /* issue VARFIXED event */
      SCIP_CALL( SCIPeventCreateVarFixed(&event, blkmem, var) );
      SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, NULL, NULL, NULL, &event) );

      /* reset the objective value of the aggregated variable, thus adjusting the objective value of the aggregation
       * variables and the problem's objective offset
       */
      SCIP_CALL( SCIPvarAddObj(var, blkmem, set, stat, prob, primal, tree, lp, eventqueue, obj) );

      *aggregated = TRUE;
      break;

   case SCIP_VARSTATUS_COLUMN:
      SCIPerrorMessage("cannot multi-aggregate a column variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot multi-aggregate a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      SCIPerrorMessage("cannot multi-aggregate an aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot multi-aggregate a multiple aggregated variable again\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      /* aggregate negation variable x in x' = offset - x, instead of aggregating x' directly:
       *   x' = a_1*y_1 + ... + a_n*y_n + c  ->  x = offset - x' = offset - a_1*y_1 - ... - a_n*y_n - c
       */
      assert(SCIPsetIsZero(set, var->obj));
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);

      /* switch the signs of the aggregation scalars */
      for( v = 0; v < naggvars; ++v )
         scalars[v] *= -1.0;
         
      /* perform the multi aggregation on the negation variable */
      SCIP_CALL( SCIPvarMultiaggregate(var->negatedvar, blkmem, set, stat, prob, primal, tree, lp,
            cliquetable, branchcand, eventqueue, naggvars, aggvars, scalars, var->data.negate.constant - constant, 
            infeasible, aggregated) );

      /* switch the signs of the aggregation scalars again, to reset them to their original values */
      for( v = 0; v < naggvars; ++v )
         scalars[v] *= -1.0;
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
   
   return SCIP_OKAY;
}

/** gets negated variable x' = offset - x of problem variable x; the negated variable is created if not yet existing;
 *  the negation offset of binary variables is always 1, the offset of other variables is fixed to lb + ub when the
 *  negated variable is created
 */
SCIP_RETCODE SCIPvarNegate(
   SCIP_VAR*             var,                /**< problem variable to negate */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_VAR**            negvar              /**< pointer to store the negated variable */
   )
{
   assert(var != NULL);
   assert(negvar != NULL);

   /* check, if we already created the negated variable */
   if( var->negatedvar == NULL )
   {
      char negvarname[SCIP_MAXSTRLEN];

      assert(SCIPvarGetStatus(var) != SCIP_VARSTATUS_NEGATED);

      SCIPdebugMessage("creating negated variable of <%s>\n", var->name);
      
      /* negation is only possible for bounded variables */
      if( SCIPsetIsInfinity(set, -var->glbdom.lb) || SCIPsetIsInfinity(set, var->glbdom.ub) )
      {
         SCIPerrorMessage("cannot negate unbounded variable\n");
         return SCIP_INVALIDDATA;
      }

      sprintf(negvarname, "%s_neg", var->name);

      /* create negated variable */
      SCIP_CALL( varCreate(negvar, blkmem, set, stat, negvarname, var->glbdom.lb, var->glbdom.ub, 0.0,
            SCIPvarGetType(var), var->initial, var->removeable, NULL, NULL, NULL, NULL) );
      (*negvar)->varstatus = SCIP_VARSTATUS_NEGATED; /*lint !e641*/
      if( SCIPvarGetType(var) == SCIP_VARTYPE_BINARY )
         (*negvar)->data.negate.constant = 1.0;
      else
         (*negvar)->data.negate.constant = var->glbdom.lb + var->glbdom.ub;

      /* create event filter for transformed variable */
      if( SCIPvarIsTransformed(var) )
      {
         SCIP_CALL( SCIPeventfilterCreate(&(*negvar)->eventfilter, blkmem) );
      }

      /* set the bounds corresponding to the negation variable */
      (*negvar)->glbdom.lb = (*negvar)->data.negate.constant - var->glbdom.ub;
      (*negvar)->glbdom.ub = (*negvar)->data.negate.constant - var->glbdom.lb;
      (*negvar)->locdom.lb = (*negvar)->data.negate.constant - var->locdom.ub;
      (*negvar)->locdom.ub = (*negvar)->data.negate.constant - var->locdom.lb;
      /**@todo create holes in the negated variable corresponding to the holes of the negation variable */
      
      /* link the variables together */
      var->negatedvar = *negvar;
      (*negvar)->negatedvar = var;

      /* copy the branch factor and priority, and use the negative preferred branching direction */
      (*negvar)->branchfactor = var->branchfactor;
      (*negvar)->branchpriority = var->branchpriority;
      (*negvar)->branchdirection = SCIPbranchdirOpposite((SCIP_BRANCHDIR)var->branchdirection); /*lint !e641*/

      /* make negated variable a parent of the negation variable (negated variable is captured as a parent) */
      SCIP_CALL( varAddParent(var, blkmem, set, *negvar) );
      assert((*negvar)->nuses == 1);
   }
   assert(var->negatedvar != NULL);

   /* return the negated variable */
   *negvar = var->negatedvar;

   /* exactly one variable of the negation pair has to be marked as negated variable */
   assert((SCIPvarGetStatus(*negvar) == SCIP_VARSTATUS_NEGATED) != (SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED));

   return SCIP_OKAY;
}

/** informs variable that its position in problem's vars array changed */
void SCIPvarSetProbindex(
   SCIP_VAR*             var,                /**< problem variable */
   int                   probindex           /**< new problem index of variable */
   )
{
   assert(var != NULL);

   var->probindex = probindex;
   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN )
   {
      assert(var->data.col != NULL);
      var->data.col->var_probindex = probindex;
   }
}

/** marks the variable to be deleted from the problem */
void SCIPvarMarkDeleted(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(var->probindex != -1);

   var->deleted = TRUE;
}

/** changes type of variable; cannot be called, if var belongs to a problem */
SCIP_RETCODE SCIPvarChgType(
   SCIP_VAR*             var,                /**< problem variable to change */
   SCIP_VARTYPE          vartype             /**< new type of variable */
   )
{
   assert(var != NULL);

   SCIPdebugMessage("change type of <%s> from %d to %d\n", var->name, SCIPvarGetType(var), vartype);

   if( var->probindex >= 0 )
   {
      SCIPerrorMessage("cannot change type of variable already in the problem\n");
      return SCIP_INVALIDDATA;
   }
   
   var->vartype = vartype; /*lint !e641*/
   if( var->negatedvar != NULL )
      var->negatedvar->vartype = vartype; /*lint !e641*/

   return SCIP_OKAY;
}

/** appends OBJCHANGED event to the event queue */
static
SCIP_RETCODE varEventObjChanged(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             oldobj,             /**< old objective value for variable */
   SCIP_Real             newobj              /**< new objective value for variable */
   )
{
   SCIP_EVENT* event;

   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE);
   assert(SCIPvarIsTransformed(var));
   assert(!SCIPsetIsEQ(set, oldobj, newobj));

   SCIP_CALL( SCIPeventCreateObjChanged(&event, blkmem, var, oldobj, newobj) );
   SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, primal, lp, NULL, NULL, &event) );

   return SCIP_OKAY;
}

/** changes objective value of variable */
SCIP_RETCODE SCIPvarChgObj(
   SCIP_VAR*             var,                /**< variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newobj              /**< new objective value for variable */
   )
{
   SCIP_Real oldobj;

   assert(var != NULL);
   assert(set != NULL);

   SCIPdebugMessage("changing objective value of <%s> from %g to %g\n", var->name, var->obj, newobj);

   if( !SCIPsetIsEQ(set, var->obj, newobj) )
   {
      switch( SCIPvarGetStatus(var) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         if( var->data.original.transvar != NULL )
         {
            SCIP_CALL( SCIPvarChgObj(var->data.original.transvar, blkmem, set, primal, lp, eventqueue, newobj) );
         }
         else
         {
            assert(set->stage == SCIP_STAGE_PROBLEM);
            var->obj = newobj;
         }
         break;

      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_COLUMN:
         oldobj = var->obj;
         var->obj = newobj;
         SCIP_CALL( varEventObjChanged(var, blkmem, set, primal, lp, eventqueue, oldobj, var->obj) );
         break;

      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_AGGREGATED:
      case SCIP_VARSTATUS_MULTAGGR:
      case SCIP_VARSTATUS_NEGATED:
         SCIPerrorMessage("cannot change objective value of a fixed, aggregated, multi-aggregated, or negated variable\n");
         return SCIP_INVALIDDATA;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** adds value to objective value of variable */
SCIP_RETCODE SCIPvarAddObj(
   SCIP_VAR*             var,                /**< variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< transformed problem after presolve */
   SCIP_PRIMAL*          primal,             /**< primal data */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             addobj              /**< additional objective value for variable */
   )
{
   assert(var != NULL);
   assert(set != NULL);
   assert(set->stage < SCIP_STAGE_INITSOLVE);

   SCIPdebugMessage("adding %g to objective value %g of <%s>\n", addobj, var->obj, var->name);

   if( !SCIPsetIsZero(set, addobj) )
   {
      SCIP_Real oldobj;
      int i;

      switch( SCIPvarGetStatus(var) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         if( var->data.original.transvar != NULL )
         {
            SCIP_CALL( SCIPvarAddObj(var->data.original.transvar, blkmem, set, stat, prob, primal, tree, lp, eventqueue, addobj) );
         }
         else
         {
            assert(set->stage == SCIP_STAGE_PROBLEM);
            var->obj += addobj;
         }
         break;

      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_COLUMN:
         oldobj = var->obj;
         var->obj += addobj;
         SCIP_CALL( varEventObjChanged(var, blkmem, set, primal, lp, eventqueue, oldobj, var->obj) );
         break;

      case SCIP_VARSTATUS_FIXED:
         assert(SCIPsetIsEQ(set, var->locdom.lb, var->locdom.ub));
         SCIPprobAddObjoffset(prob, var->locdom.lb * addobj);
         SCIP_CALL( SCIPprimalUpdateObjoffset(primal, blkmem, set, stat, prob, tree, lp) );
         break;

      case SCIP_VARSTATUS_AGGREGATED:
         /* x = a*y + c  ->  add a*addobj to obj. val. of y, and c*addobj to obj. offset of problem */
         SCIPprobAddObjoffset(prob, var->data.aggregate.constant * addobj);
         SCIP_CALL( SCIPprimalUpdateObjoffset(primal, blkmem, set, stat, prob, tree, lp) );
         SCIP_CALL( SCIPvarAddObj(var->data.aggregate.var, blkmem, set, stat, prob, primal, tree, lp, eventqueue,
                        var->data.aggregate.scalar * addobj) );
         break;

      case SCIP_VARSTATUS_MULTAGGR:
         /* x = a_1*y_1 + ... + a_n*y_n  + c  ->  add a_i*addobj to obj. val. of y_i, and c*addobj to obj. offset */
         SCIPprobAddObjoffset(prob, var->data.multaggr.constant * addobj);
         SCIP_CALL( SCIPprimalUpdateObjoffset(primal, blkmem, set, stat, prob, tree, lp) );
         for( i = 0; i < var->data.multaggr.nvars; ++i )
         {
            SCIP_CALL( SCIPvarAddObj(var->data.multaggr.vars[i], blkmem, set, stat, prob, primal, tree, lp, 
                           eventqueue, var->data.multaggr.scalars[i] * addobj) );
         }
         break;

      case SCIP_VARSTATUS_NEGATED:
         /* x' = offset - x  ->  add -addobj to obj. val. of x and offset*addobj to obj. offset of problem */
         assert(var->negatedvar != NULL);
         assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert(var->negatedvar->negatedvar == var);
         SCIPprobAddObjoffset(prob, var->data.negate.constant * addobj);
         SCIP_CALL( SCIPprimalUpdateObjoffset(primal, blkmem, set, stat, prob, tree, lp) );
         SCIP_CALL( SCIPvarAddObj(var->negatedvar, blkmem, set, stat, prob, primal, tree, lp, eventqueue, -addobj) );
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** changes objective value of variable in current dive */
SCIP_RETCODE SCIPvarChgObjDive(
   SCIP_VAR*             var,                /**< problem variable to change */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_Real             newobj              /**< new objective value for variable */
   )
{
   assert(var != NULL);
   assert(lp != NULL);

   SCIPdebugMessage("changing objective of <%s> to %g in current dive\n", var->name, newobj);

   if( SCIPsetIsZero(set, newobj) )
      newobj = 0.0;

   /* change objective value of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      SCIP_CALL( SCIPvarChgObjDive(var->data.original.transvar, set, lp, newobj) );
      break;
         
   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      SCIP_CALL( SCIPcolChgObj(var->data.col, set, lp, newobj) );
      break;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      /* nothing to do here: only the constant shift in objective function would change */
      break;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      assert(!SCIPsetIsZero(set, var->data.aggregate.scalar));
      SCIP_CALL( SCIPvarChgObjDive(var->data.aggregate.var, set, lp, newobj / var->data.aggregate.scalar) );
      /* the constant can be ignored, because it would only affect the objective shift */
      break;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot change diving objective value of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgObjDive(var->negatedvar, set, lp, -newobj) );
      /* the offset can be ignored, because it would only affect the objective shift */
      break;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** adjust lower bound to integral value, if variable is integral */
void SCIPvarAdjustLb(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real*            lb                  /**< pointer to lower bound to adjust */
   )
{
   assert(var != NULL);
   assert(lb != NULL);

   SCIPdebugMessage("adjust lower bound %g of <%s>\n", *lb, var->name);

   *lb = adjustedLb(set, SCIPvarGetType(var), *lb);
}

/** adjust upper bound to integral value, if variable is integral */
void SCIPvarAdjustUb(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real*            ub                  /**< pointer to upper bound to adjust */
   )
{
   assert(var != NULL);
   assert(ub != NULL);

   SCIPdebugMessage("adjust upper bound %g of <%s>\n", *ub, var->name);

   *ub = adjustedUb(set, SCIPvarGetType(var), *ub);
}

/** adjust lower or upper bound to integral value, if variable is integral */
void SCIPvarAdjustBd(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BOUNDTYPE        boundtype,          /**< type of bound to adjust */
   SCIP_Real*            bd                  /**< pointer to bound to adjust */
   )
{
   assert(boundtype == SCIP_BOUNDTYPE_LOWER || boundtype == SCIP_BOUNDTYPE_UPPER);

   if( boundtype == SCIP_BOUNDTYPE_LOWER )
      SCIPvarAdjustLb(var, set, bd);
   else
      SCIPvarAdjustUb(var, set, bd);
}

/** changes lower bound of original variable in original problem */
SCIP_RETCODE SCIPvarChgLbOriginal(
   SCIP_VAR*             var,                /**< problem variable to change */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   int i;

   assert(var != NULL);
   assert(!SCIPvarIsTransformed(var));
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL || SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);
   assert(set != NULL);
   assert(set->stage == SCIP_STAGE_PROBLEM);

   SCIPdebugMessage("changing original lower bound of <%s> from %g to %g\n", 
      var->name, var->data.original.origdom.lb, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   if( SCIPsetIsEQ(set, var->data.original.origdom.lb, newbound) )
      return SCIP_OKAY;

   /* change the bound */
   var->data.original.origdom.lb = newbound;

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      SCIP_VAR* parentvar;

      parentvar = var->parentvars[i];
      assert(parentvar != NULL);
      assert(SCIPvarGetStatus(parentvar) == SCIP_VARSTATUS_NEGATED);
      assert(parentvar->negatedvar == var);
      assert(var->negatedvar == parentvar);

      SCIP_CALL( SCIPvarChgUbOriginal(parentvar, set, parentvar->data.negate.constant - newbound) );
   }

   return SCIP_OKAY;
}

/** changes upper bound of original variable in original problem */
SCIP_RETCODE SCIPvarChgUbOriginal(
   SCIP_VAR*             var,                /**< problem variable to change */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   int i;

   assert(var != NULL);
   assert(!SCIPvarIsTransformed(var));
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL || SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);
   assert(set != NULL);
   assert(set->stage == SCIP_STAGE_PROBLEM);

   SCIPdebugMessage("changing original upper bound of <%s> from %g to %g\n", 
      var->name, var->data.original.origdom.ub, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   if( SCIPsetIsEQ(set, var->data.original.origdom.ub, newbound) )
      return SCIP_OKAY;

   /* change the bound */
   var->data.original.origdom.ub = newbound;

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      SCIP_VAR* parentvar;

      parentvar = var->parentvars[i];
      assert(parentvar != NULL);
      assert(SCIPvarGetStatus(parentvar) == SCIP_VARSTATUS_NEGATED);
      assert(parentvar->negatedvar == var);
      assert(var->negatedvar == parentvar);

      SCIP_CALL( SCIPvarChgLbOriginal(parentvar, set, parentvar->data.negate.constant - newbound) );
   }

   return SCIP_OKAY;
}

/** appends GLBCHANGED event to the event queue */
static
SCIP_RETCODE varEventGlbChanged(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             oldbound,           /**< old lower bound for variable */
   SCIP_Real             newbound            /**< new lower bound for variable */
   )
{
   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert(SCIPvarIsTransformed(var));
   assert(!SCIPsetIsEQ(set, oldbound, newbound));

   /* check, if the variable is being tracked for bound changes */
   if( (var->eventfilter->len > 0 && (var->eventfilter->eventmask & SCIP_EVENTTYPE_GLBCHANGED) != 0) )
   {
      SCIP_EVENT* event;

      SCIPdebugMessage("issue GLBCHANGED event for variable <%s>: %g -> %g\n", var->name, oldbound, newbound);

      SCIP_CALL( SCIPeventCreateGlbChanged(&event, blkmem, var, oldbound, newbound) );
      SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, lp, branchcand, NULL, &event) );
   }

   return SCIP_OKAY;
}

/** appends GUBCHANGED event to the event queue */
static
SCIP_RETCODE varEventGubChanged(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             oldbound,           /**< old lower bound for variable */
   SCIP_Real             newbound            /**< new lower bound for variable */
   )
{
   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert(SCIPvarIsTransformed(var));
   assert(!SCIPsetIsEQ(set, oldbound, newbound));

   /* check, if the variable is being tracked for bound changes */
   if( (var->eventfilter->len > 0 && (var->eventfilter->eventmask & SCIP_EVENTTYPE_GUBCHANGED) != 0) )
   {
      SCIP_EVENT* event;

      SCIPdebugMessage("issue GUBCHANGED event for variable <%s>: %g -> %g\n", var->name, oldbound, newbound);

      SCIP_CALL( SCIPeventCreateGubChanged(&event, blkmem, var, oldbound, newbound) );
      SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, lp, branchcand, NULL, &event) );
   }

   return SCIP_OKAY;
}


/* forward declaration, because both methods call each other recursively */

/** performs the current change in upper bound, changes all parents accordingly */
static            
SCIP_RETCODE varProcessChgUbGlobal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   );

/** performs the current change in lower bound, changes all parents accordingly */
static            
SCIP_RETCODE varProcessChgLbGlobal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   SCIP_VAR* parentvar;
   SCIP_Real oldbound;
   int i;

   assert(var != NULL);
   assert(SCIPsetIsEQ(set, newbound, adjustedLb(set, SCIPvarGetType(var), newbound)));
   assert(var->glbdom.lb <= var->locdom.lb);
   assert(var->locdom.ub <= var->glbdom.ub);

   SCIPdebugMessage("process changing global lower bound of <%s> from %f to %f\n", var->name, var->glbdom.lb, newbound);

   if( SCIPsetIsEQ(set, newbound, var->glbdom.lb) )
      return SCIP_OKAY;

   /* check bound on debugging solution */
   SCIP_CALL( SCIPdebugCheckLbGlobal(set, var, newbound) ); /*lint !e506 !e774*/

   /* change the bound */
   oldbound = var->glbdom.lb;
   var->glbdom.lb = newbound;
   assert(var->glbdom.lb <= var->locdom.lb);
   assert(var->locdom.ub <= var->glbdom.ub);

   /* remove redundant implications and variable bounds */
   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE )
   {
      SCIP_CALL( varRemoveImplicsVbs(var, blkmem, set, TRUE, TRUE) );
   }

   /* issue bound change event */
   assert(SCIPvarIsTransformed(var) == (var->eventfilter != NULL));
   if( var->eventfilter != NULL )
   {
      SCIP_CALL( varEventGlbChanged(var, blkmem, set, lp, branchcand, eventqueue, oldbound, newbound) );
   }

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         SCIP_CALL( varProcessChgLbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue, newbound) );
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         return SCIP_INVALIDDATA;
      
      case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
         assert(parentvar->data.aggregate.var == var);
         if( SCIPsetIsPositive(set, parentvar->data.aggregate.scalar) )
         {
            /* a > 0 -> change lower bound of y */
            assert((SCIPsetIsInfinity(set, -parentvar->glbdom.lb) && SCIPsetIsInfinity(set, -oldbound))
               || SCIPsetIsEQ(set, parentvar->glbdom.lb,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgLbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue,
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         else
         {
            /* a < 0 -> change upper bound of y */
            assert(SCIPsetIsNegative(set, parentvar->data.aggregate.scalar));
            assert((SCIPsetIsInfinity(set, parentvar->glbdom.ub) && SCIPsetIsInfinity(set, -oldbound))
               || SCIPsetIsEQ(set, parentvar->glbdom.ub,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgUbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue,
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         break;

      case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
         assert(parentvar->negatedvar != NULL);
         assert(SCIPvarGetStatus(parentvar->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert(parentvar->negatedvar->negatedvar == parentvar);
         SCIP_CALL( varProcessChgUbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue, 
               parentvar->data.negate.constant - newbound) );
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** performs the current change in upper bound, changes all parents accordingly */
static            
SCIP_RETCODE varProcessChgUbGlobal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   SCIP_VAR* parentvar;
   SCIP_Real oldbound;
   int i;

   assert(var != NULL);
   assert(SCIPsetIsEQ(set, newbound, adjustedUb(set, SCIPvarGetType(var), newbound)));
   assert(var->glbdom.lb <= var->locdom.lb);
   assert(var->locdom.ub <= var->glbdom.ub);

   SCIPdebugMessage("process changing global upper bound of <%s> from %f to %f\n", var->name, var->glbdom.ub, newbound);

   if( SCIPsetIsEQ(set, newbound, var->glbdom.ub) )
      return SCIP_OKAY;

   /* check bound on debugging solution */
   SCIP_CALL( SCIPdebugCheckUbGlobal(set, var, newbound) ); /*lint !e506 !e774*/

   /* change the bound */
   oldbound = var->glbdom.ub;
   var->glbdom.ub = newbound;
   assert(var->glbdom.lb <= var->locdom.lb);
   assert(var->locdom.ub <= var->glbdom.ub);

   /* remove redundant implications and variable bounds */
   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE )
   {
      SCIP_CALL( varRemoveImplicsVbs(var, blkmem, set, TRUE, TRUE) );
   }

   /* issue bound change event */
   assert(SCIPvarIsTransformed(var) == (var->eventfilter != NULL));
   if( var->eventfilter != NULL )
   {
      SCIP_CALL( varEventGubChanged(var, blkmem, set, lp, branchcand, eventqueue, oldbound, newbound) );
   }

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         SCIP_CALL( varProcessChgUbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue, newbound) );
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         return SCIP_INVALIDDATA;
      
      case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
         assert(parentvar->data.aggregate.var == var);
         if( SCIPsetIsPositive(set, parentvar->data.aggregate.scalar) )
         {
            /* a > 0 -> change upper bound of y */
            assert((SCIPsetIsInfinity(set, parentvar->glbdom.ub) && SCIPsetIsInfinity(set, oldbound))
               || SCIPsetIsEQ(set, parentvar->glbdom.ub,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgUbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue,
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         else 
         {
            /* a < 0 -> change lower bound of y */
            assert(SCIPsetIsNegative(set, parentvar->data.aggregate.scalar));
            assert((SCIPsetIsInfinity(set, -parentvar->glbdom.lb) && SCIPsetIsInfinity(set, oldbound))
               || SCIPsetIsEQ(set, parentvar->glbdom.lb,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgLbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue,
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         break;

      case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
         assert(parentvar->negatedvar != NULL);
         assert(SCIPvarGetStatus(parentvar->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert(parentvar->negatedvar->negatedvar == parentvar);
         SCIP_CALL( varProcessChgLbGlobal(parentvar, blkmem, set, lp, branchcand, eventqueue,
               parentvar->data.negate.constant - newbound) );
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** changes global lower bound of variable; if possible, adjusts bound to integral value;
 *  updates local lower bound if the global bound is tighter
 */
SCIP_RETCODE SCIPvarChgLbGlobal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   assert(var != NULL);
   assert(set != NULL);

   /* adjust bound for integral variables */
   SCIPvarAdjustLb(var, set, &newbound);

   SCIPdebugMessage("changing global lower bound of <%s> from %g to %g\n", var->name, var->glbdom.lb, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   if( SCIPsetIsEQ(set, var->glbdom.lb, newbound) )
      return SCIP_OKAY;

   /* change bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
      {
         SCIP_CALL( SCIPvarChgLbGlobal(var->data.original.transvar, blkmem, set, stat, lp, branchcand, eventqueue,
               newbound) );
      }
      else
      {
         assert(set->stage == SCIP_STAGE_PROBLEM);
         if( newbound > SCIPvarGetLbLocal(var) )
         {
            SCIP_CALL( SCIPvarChgLbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
         }
         SCIP_CALL( varProcessChgLbGlobal(var, blkmem, set, lp, branchcand, eventqueue, newbound) );
      }
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      if( newbound > SCIPvarGetLbLocal(var) )
      {
         SCIP_CALL( SCIPvarChgLbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      }
      SCIP_CALL( varProcessChgLbGlobal(var, blkmem, set, lp, branchcand, eventqueue, newbound) );
      break;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot change the bounds of a fixed variable\n");
      return SCIP_INVALIDDATA;
         
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> change lower bound of y */
         assert((SCIPsetIsInfinity(set, -var->glbdom.lb) && SCIPsetIsInfinity(set, -var->data.aggregate.var->glbdom.lb))
            || SCIPsetIsEQ(set, var->glbdom.lb,
               var->data.aggregate.var->glbdom.lb * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgLbGlobal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue,
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> change upper bound of y */
         assert((SCIPsetIsInfinity(set, -var->glbdom.lb) && SCIPsetIsInfinity(set, var->data.aggregate.var->glbdom.ub))
            || SCIPsetIsEQ(set, var->glbdom.lb,
               var->data.aggregate.var->glbdom.ub * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgUbGlobal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue,
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo change the sides of the corresponding linear constraint */
      SCIPerrorMessage("changing the bounds of a multiple aggregated variable is not implemented yet\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgUbGlobal(var->negatedvar, blkmem, set, stat, lp, branchcand, eventqueue,
            var->data.negate.constant - newbound) );
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** changes global upper bound of variable; if possible, adjusts bound to integral value;
 *  updates local upper bound if the global bound is tighter
 */
SCIP_RETCODE SCIPvarChgUbGlobal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   assert(var != NULL);
   assert(set != NULL);

   /* adjust bound for integral variables */
   SCIPvarAdjustUb(var, set, &newbound);

   SCIPdebugMessage("changing global upper bound of <%s> from %g to %g\n", var->name, var->glbdom.ub, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   if( SCIPsetIsEQ(set, var->glbdom.ub, newbound) )
      return SCIP_OKAY;

   /* change bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
      {
         SCIP_CALL( SCIPvarChgUbGlobal(var->data.original.transvar, blkmem, set, stat, lp, branchcand, eventqueue,
               newbound) );
      }
      else
      {
         assert(set->stage == SCIP_STAGE_PROBLEM);
         if( newbound < SCIPvarGetUbLocal(var) )
         {
            SCIP_CALL( SCIPvarChgUbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
         }
         SCIP_CALL( varProcessChgUbGlobal(var, blkmem, set, lp, branchcand, eventqueue, newbound) );
      }
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      if( newbound < SCIPvarGetUbLocal(var) )
      {
         SCIP_CALL( SCIPvarChgUbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      }
      SCIP_CALL( varProcessChgUbGlobal(var, blkmem, set, lp, branchcand, eventqueue, newbound) );
      break;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot change the bounds of a fixed variable\n");
      return SCIP_INVALIDDATA;
         
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> change lower bound of y */
         assert((SCIPsetIsInfinity(set, var->glbdom.ub) && SCIPsetIsInfinity(set, var->data.aggregate.var->glbdom.ub))
            || SCIPsetIsEQ(set, var->glbdom.ub,
               var->data.aggregate.var->glbdom.ub * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgUbGlobal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue,
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> change upper bound of y */
         assert((SCIPsetIsInfinity(set, var->glbdom.ub) && SCIPsetIsInfinity(set, -var->data.aggregate.var->glbdom.lb))
            || SCIPsetIsEQ(set, var->glbdom.ub,
               var->data.aggregate.var->glbdom.lb * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgLbGlobal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue,
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo change the sides of the corresponding linear constraint */
      SCIPerrorMessage("changing the bounds of a multiple aggregated variable is not implemented yet\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgLbGlobal(var->negatedvar, blkmem, set, stat, lp, branchcand, eventqueue,
            var->data.negate.constant - newbound) );
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** changes global bound of variable; if possible, adjusts bound to integral value;
 *  updates local bound if the global bound is tighter
 */
SCIP_RETCODE SCIPvarChgBdGlobal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound,           /**< new bound for variable */
   SCIP_BOUNDTYPE        boundtype           /**< type of bound: lower or upper bound */
   )
{
   /* apply bound change to the LP data */
   switch( boundtype )
   {
   case SCIP_BOUNDTYPE_LOWER:
      return SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound);
   case SCIP_BOUNDTYPE_UPPER:
      return SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound);
   default:
      SCIPerrorMessage("unknown bound type\n");
      return SCIP_INVALIDDATA;
   }
}

/** appends LBTIGHTENED or LBRELAXED event to the event queue */
static
SCIP_RETCODE varEventLbChanged(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             oldbound,           /**< old lower bound for variable */
   SCIP_Real             newbound            /**< new lower bound for variable */
   )
{
   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert(SCIPvarIsTransformed(var));
   assert(!SCIPsetIsEQ(set, oldbound, newbound));

   /* check, if the variable is being tracked for bound changes
    * COLUMN and LOOSE variables are tracked always, because row activities and LP changes have to be updated
    */
   if( (var->eventfilter->len > 0 && (var->eventfilter->eventmask & SCIP_EVENTTYPE_LBCHANGED) != 0)
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE )
   {
      SCIP_EVENT* event;

      SCIPdebugMessage("issue LBCHANGED event for variable <%s>: %g -> %g\n", var->name, oldbound, newbound);

      SCIP_CALL( SCIPeventCreateLbChanged(&event, blkmem, var, oldbound, newbound) );
      SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, lp, branchcand, NULL, &event) );
   }

   return SCIP_OKAY;
}

/** appends UBTIGHTENED or UBRELAXED event to the event queue */
static
SCIP_RETCODE varEventUbChanged(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             oldbound,           /**< old upper bound for variable */
   SCIP_Real             newbound            /**< new upper bound for variable */
   )
{
   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert(SCIPvarIsTransformed(var));
   assert(!SCIPsetIsEQ(set, oldbound, newbound));

   /* check, if the variable is being tracked for bound changes
    * COLUMN and LOOSE variables are tracked always, because row activities and LP changes have to be updated
    */
   if( (var->eventfilter->len > 0 && (var->eventfilter->eventmask & SCIP_EVENTTYPE_UBCHANGED) != 0)
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE )
   {
      SCIP_EVENT* event;
   
      SCIPdebugMessage("issue UBCHANGED event for variable <%s>: %g -> %g\n", var->name, oldbound, newbound);

      SCIP_CALL( SCIPeventCreateUbChanged(&event, blkmem, var, oldbound, newbound) );
      SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, lp, branchcand, NULL, &event) );
   }

   return SCIP_OKAY;
}

/* forward declaration, because both methods call each other recursively */

/** performs the current change in upper bound, changes all parents accordingly */
static            
SCIP_RETCODE varProcessChgUbLocal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   );

/** performs the current change in lower bound, changes all parents accordingly */
static            
SCIP_RETCODE varProcessChgLbLocal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   SCIP_VAR* parentvar;
   SCIP_Real oldbound;
   int i;

   assert(var != NULL);
   assert(SCIPsetIsEQ(set, newbound, adjustedLb(set, SCIPvarGetType(var), newbound)));

   SCIPdebugMessage("process changing lower bound of <%s> from %g to %g\n", var->name, var->locdom.lb, newbound);

   if( SCIPsetIsEQ(set, newbound, var->locdom.lb) )
      return SCIP_OKAY;
   if( SCIPsetIsEQ(set, newbound, var->glbdom.lb) )
      newbound = var->glbdom.lb;

   /* change the bound */
   oldbound = var->locdom.lb;
   var->locdom.lb = newbound;

   /* issue bound change event */
   assert(SCIPvarIsTransformed(var) == (var->eventfilter != NULL));
   if( var->eventfilter != NULL )
   {
      SCIP_CALL( varEventLbChanged(var, blkmem, set, lp, branchcand, eventqueue, oldbound, newbound) );
   }

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         SCIP_CALL( varProcessChgLbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         return SCIP_INVALIDDATA;
      
      case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
         assert(parentvar->data.aggregate.var == var);
         if( SCIPsetIsPositive(set, parentvar->data.aggregate.scalar) )
         {
            /* a > 0 -> change lower bound of y */
            assert((SCIPsetIsInfinity(set, -parentvar->locdom.lb) && SCIPsetIsInfinity(set, -oldbound))
               || SCIPsetIsEQ(set, parentvar->locdom.lb,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgLbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue, 
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         else 
         {
            /* a < 0 -> change upper bound of y */
            assert(SCIPsetIsNegative(set, parentvar->data.aggregate.scalar));
            assert((SCIPsetIsInfinity(set, parentvar->locdom.ub) && SCIPsetIsInfinity(set, -oldbound))
               || SCIPsetIsEQ(set, parentvar->locdom.ub,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgUbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue, 
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         break;

      case SCIP_VARSTATUS_NEGATED: /* x = offset - x'  ->  x' = offset - x */
         assert(parentvar->negatedvar != NULL);
         assert(SCIPvarGetStatus(parentvar->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert(parentvar->negatedvar->negatedvar == parentvar);
         SCIP_CALL( varProcessChgUbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue,
               parentvar->data.negate.constant - newbound) );
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** performs the current change in upper bound, changes all parents accordingly */
static            
SCIP_RETCODE varProcessChgUbLocal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   SCIP_VAR* parentvar;
   SCIP_Real oldbound;
   int i;

   assert(var != NULL);
   assert(SCIPsetIsEQ(set, newbound, adjustedUb(set, SCIPvarGetType(var), newbound)));

   SCIPdebugMessage("process changing upper bound of <%s> from %g to %g\n", var->name, var->locdom.ub, newbound);

   if( SCIPsetIsEQ(set, newbound, var->locdom.ub) )
      return SCIP_OKAY;
   if( SCIPsetIsEQ(set, newbound, var->glbdom.ub) )
      newbound = var->glbdom.ub;

   /* change the bound */
   oldbound = var->locdom.ub;
   var->locdom.ub = newbound;

   /* issue bound change event */
   assert(SCIPvarIsTransformed(var) == (var->eventfilter != NULL));
   if( var->eventfilter != NULL )
   {
      SCIP_CALL( varEventUbChanged(var, blkmem, set, lp, branchcand, eventqueue, oldbound, newbound) );
   }

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         SCIP_CALL( varProcessChgUbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         return SCIP_INVALIDDATA;
      
      case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
         assert(parentvar->data.aggregate.var == var);
         if( SCIPsetIsPositive(set, parentvar->data.aggregate.scalar) )
         {
            /* a > 0 -> change upper bound of x */
            assert((SCIPsetIsInfinity(set, parentvar->locdom.ub) && SCIPsetIsInfinity(set, oldbound))
               || SCIPsetIsEQ(set, parentvar->locdom.ub,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgUbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue, 
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         else
         {
            /* a < 0 -> change lower bound of x */
            assert(SCIPsetIsNegative(set, parentvar->data.aggregate.scalar));
            assert((SCIPsetIsInfinity(set, -parentvar->locdom.lb) && SCIPsetIsInfinity(set, oldbound))
               || SCIPsetIsEQ(set, parentvar->locdom.lb,
                  oldbound * parentvar->data.aggregate.scalar + parentvar->data.aggregate.constant));
            SCIP_CALL( varProcessChgLbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue, 
                  parentvar->data.aggregate.scalar * newbound + parentvar->data.aggregate.constant) );
         }
         break;

      case SCIP_VARSTATUS_NEGATED: /* x = offset - x'  ->  x' = offset - x */
         assert(parentvar->negatedvar != NULL);
         assert(SCIPvarGetStatus(parentvar->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert(parentvar->negatedvar->negatedvar == parentvar);
         SCIP_CALL( varProcessChgLbLocal(parentvar, blkmem, set, stat, lp, branchcand, eventqueue,
               parentvar->data.negate.constant - newbound) );
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   return SCIP_OKAY;
}

/** changes current local lower bound of variable; if possible, adjusts bound to integral value; stores inference
 *  information in variable
 */
SCIP_RETCODE SCIPvarChgLbLocal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   assert(var != NULL);
   assert(set != NULL);

   /* adjust bound for integral variables */
   SCIPvarAdjustLb(var, set, &newbound);
   assert(SCIPsetIsLE(set, newbound, var->locdom.ub));

   SCIPdebugMessage("changing lower bound of <%s>[%g,%g] to %g\n", var->name, var->locdom.lb, var->locdom.ub, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   if( SCIPsetIsEQ(set, var->locdom.lb, newbound) )
      return SCIP_OKAY;

   /* change bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
      {
         SCIP_CALL( SCIPvarChgLbLocal(var->data.original.transvar, blkmem, set, stat, lp, branchcand, eventqueue, 
               newbound) );
      }
      else
      {
         assert(set->stage == SCIP_STAGE_PROBLEM);
         stat->domchgcount++;
         SCIP_CALL( varProcessChgLbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      }
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      stat->domchgcount++;
      SCIP_CALL( varProcessChgLbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      break;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot change the bounds of a fixed variable\n");
      return SCIP_INVALIDDATA;
         
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> change lower bound of y */
         assert((SCIPsetIsInfinity(set, -var->locdom.lb) && SCIPsetIsInfinity(set, -var->data.aggregate.var->locdom.lb))
            || SCIPsetIsEQ(set, var->locdom.lb,
               var->data.aggregate.var->locdom.lb * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgLbLocal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> change upper bound of y */
         assert((SCIPsetIsInfinity(set, -var->locdom.lb) && SCIPsetIsInfinity(set, var->data.aggregate.var->locdom.ub))
            || SCIPsetIsEQ(set, var->locdom.lb,
               var->data.aggregate.var->locdom.ub * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgUbLocal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo change the sides of the corresponding linear constraint */
      SCIPerrorMessage("changing the bounds of a multiple aggregated variable is not implemented yet\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgUbLocal(var->negatedvar, blkmem, set, stat, lp, branchcand, eventqueue, 
            var->data.negate.constant - newbound) );
      break;
         
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
   
   return SCIP_OKAY;
}

/** changes current local upper bound of variable; if possible, adjusts bound to integral value; stores inference
 *  information in variable
 */
SCIP_RETCODE SCIPvarChgUbLocal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   assert(var != NULL);
   assert(set != NULL);

   /* adjust bound for integral variables */
   SCIPvarAdjustUb(var, set, &newbound);
   assert(SCIPsetIsGE(set, newbound, var->locdom.lb));

   SCIPdebugMessage("changing upper bound of <%s>[%g,%g] to %g\n", var->name, var->locdom.lb, var->locdom.ub, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;
   
   if( SCIPsetIsEQ(set, var->locdom.ub, newbound) )
      return SCIP_OKAY;

   /* change bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
      {
         SCIP_CALL( SCIPvarChgUbLocal(var->data.original.transvar, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      }
      else
      {
         assert(set->stage == SCIP_STAGE_PROBLEM);
         stat->domchgcount++;
         SCIP_CALL( varProcessChgUbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      }
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      stat->domchgcount++;
      SCIP_CALL( varProcessChgUbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound) );
      break;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot change the bounds of a fixed variable\n");
      return SCIP_INVALIDDATA;
         
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> change upper bound of y */
         assert((SCIPsetIsInfinity(set, var->locdom.ub) && SCIPsetIsInfinity(set, var->data.aggregate.var->locdom.ub))
            || SCIPsetIsEQ(set, var->locdom.ub,
               var->data.aggregate.var->locdom.ub * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgUbLocal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> change lower bound of y */
         assert((SCIPsetIsInfinity(set, var->locdom.ub) && SCIPsetIsInfinity(set, -var->data.aggregate.var->locdom.lb))
            || SCIPsetIsEQ(set, var->locdom.ub,
               var->data.aggregate.var->locdom.lb * var->data.aggregate.scalar + var->data.aggregate.constant));
         SCIP_CALL( SCIPvarChgLbLocal(var->data.aggregate.var, blkmem, set, stat, lp, branchcand, eventqueue, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo change the sides of the corresponding linear constraint */
      SCIPerrorMessage("changing the bounds of a multiple aggregated variable is not implemented yet\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgLbLocal(var->negatedvar, blkmem, set, stat, lp, branchcand, eventqueue, 
            var->data.negate.constant - newbound) );
      break;
         
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** changes current local bound of variable; if possible, adjusts bound to integral value; stores inference
 *  information in variable
 */
SCIP_RETCODE SCIPvarChgBdLocal(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Real             newbound,           /**< new bound for variable */
   SCIP_BOUNDTYPE        boundtype           /**< type of bound: lower or upper bound */
   )
{
   /* apply bound change to the LP data */
   switch( boundtype )
   {
   case SCIP_BOUNDTYPE_LOWER:
      return SCIPvarChgLbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound);
   case SCIP_BOUNDTYPE_UPPER:
      return SCIPvarChgUbLocal(var, blkmem, set, stat, lp, branchcand, eventqueue, newbound);
   default:
      SCIPerrorMessage("unknown bound type\n");
      return SCIP_INVALIDDATA;
   }
}

/** changes lower bound of variable in current dive; if possible, adjusts bound to integral value */
SCIP_RETCODE SCIPvarChgLbDive(
   SCIP_VAR*             var,                /**< problem variable to change */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   assert(var != NULL);
   assert(lp != NULL);
   assert(SCIPlpDiving(lp));

   /* adjust bound for integral variables */
   SCIPvarAdjustLb(var, set, &newbound);

   SCIPdebugMessage("changing lower bound of <%s> to %g in current dive\n", var->name, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   /* change bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      SCIP_CALL( SCIPvarChgLbDive(var->data.original.transvar, set, lp, newbound) );
      break;
         
   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      SCIP_CALL( SCIPcolChgLb(var->data.col, set, lp, newbound) );
      break;

   case SCIP_VARSTATUS_LOOSE:
      SCIPerrorMessage("cannot change variable's bounds in dive for LOOSE variables\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot change the bounds of a fixed variable\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> change lower bound of y */
         SCIP_CALL( SCIPvarChgLbDive(var->data.aggregate.var, set, lp, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> change upper bound of y */
         SCIP_CALL( SCIPvarChgUbDive(var->data.aggregate.var, set, lp, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
      
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo change the sides of the corresponding linear constraint */
      SCIPerrorMessage("changing the bounds of a multiple aggregated variable is not implemented yet\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgUbDive(var->negatedvar, set, lp, var->data.negate.constant - newbound) );
      break;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** changes upper bound of variable in current dive; if possible, adjusts bound to integral value */
SCIP_RETCODE SCIPvarChgUbDive(
   SCIP_VAR*             var,                /**< problem variable to change */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_Real             newbound            /**< new bound for variable */
   )
{
   assert(var != NULL);
   assert(lp != NULL);
   assert(SCIPlpDiving(lp));

   /* adjust bound for integral variables */
   SCIPvarAdjustUb(var, set, &newbound);

   SCIPdebugMessage("changing upper bound of <%s> to %g in current dive\n", var->name, newbound);

   if( SCIPsetIsZero(set, newbound) )
      newbound = 0.0;

   /* change bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      SCIP_CALL( SCIPvarChgUbDive(var->data.original.transvar, set, lp, newbound) );
      break;
         
   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      SCIP_CALL( SCIPcolChgUb(var->data.col, set, lp, newbound) );
      break;

   case SCIP_VARSTATUS_LOOSE:
      SCIPerrorMessage("cannot change variable's bounds in dive for LOOSE variables\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot change the bounds of a fixed variable\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> change upper bound of y */
         SCIP_CALL( SCIPvarChgUbDive(var->data.aggregate.var, set, lp, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> change lower bound of y */
         SCIP_CALL( SCIPvarChgLbDive(var->data.aggregate.var, set, lp, 
               (newbound - var->data.aggregate.constant)/var->data.aggregate.scalar) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
      
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo change the sides of the corresponding linear constraint */
      SCIPerrorMessage("changing the bounds of a multiple aggregated variable is not implemented yet\n");
      return SCIP_INVALIDDATA;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarChgLbDive(var->negatedvar, set, lp, var->data.negate.constant - newbound) );
      break;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** adds a hole to the original variable's original domain */
SCIP_RETCODE SCIPvarAddHoleOriginal(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             left,               /**< left bound of open interval in new hole */
   SCIP_Real             right               /**< right bound of open interval in new hole */
   )
{
   assert(var != NULL);
   assert(!SCIPvarIsTransformed(var));
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL || SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);
   assert(set != NULL);
   assert(set->stage == SCIP_STAGE_PROBLEM);

   SCIPdebugMessage("adding original hole (%g,%g) to <%s>\n", left, right, var->name);

   SCIP_CALL( domAddHole(&var->data.original.origdom, blkmem, set, left, right) );

   /**@todo add hole in parent and child variables (just like with bound changes);
    *       warning! original vars' holes are in original blkmem, transformed vars' holes in transformed blkmem
    */

   return SCIP_OKAY;
}

/** adds a hole to the variable's global domain */
SCIP_RETCODE SCIPvarAddHoleGlobal(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             left,               /**< left bound of open interval in new hole */
   SCIP_Real             right               /**< right bound of open interval in new hole */
   )
{
   assert(var != NULL);

   SCIPdebugMessage("adding global hole (%g,%g) to <%s>\n", left, right, var->name);

   SCIP_CALL( domAddHole(&var->glbdom, blkmem, set, left, right) );

   /**@todo add hole in parent and child variables (just like with bound changes);
    *       warning! original vars' holes are in original blkmem, transformed vars' holes in transformed blkmem
    */

   return SCIP_OKAY;
}

/** adds a hole to the variable's current local domain */
SCIP_RETCODE SCIPvarAddHoleLocal(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             left,               /**< left bound of open interval in new hole */
   SCIP_Real             right               /**< right bound of open interval in new hole */
   )
{
   assert(var != NULL);

   SCIPdebugMessage("adding local hole (%g,%g) to <%s>\n", left, right, var->name);

   SCIP_CALL( domAddHole(&var->locdom, blkmem, set, left, right) );

   /**@todo add hole in parent and child variables (just like with bound changes);
    *       warning! original vars' holes are in original blkmem, transformed vars' holes in transformed blkmem
    */

   return SCIP_OKAY;
}

/** resets the global and local bounds of original variable to their original values */
SCIP_RETCODE SCIPvarResetBounds(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsOriginal(var));

   /* copy the original bounds back to the global and local bounds */
   var->glbdom.lb = var->data.original.origdom.lb;
   var->glbdom.ub = var->data.original.origdom.ub;
   var->locdom.lb = var->data.original.origdom.lb;
   var->locdom.ub = var->data.original.origdom.ub;

   /* free the global and local holelists and duplicate the original ones */
   holelistFree(&var->glbdom.holelist, blkmem);
   holelistFree(&var->locdom.holelist, blkmem);
   SCIP_CALL( holelistDuplicate(&var->glbdom.holelist, blkmem, set, var->data.original.origdom.holelist) );
   SCIP_CALL( holelistDuplicate(&var->locdom.holelist, blkmem, set, var->data.original.origdom.holelist) );

   return SCIP_OKAY;
}

/** issues a IMPLADDED event on the given variable */
static
SCIP_RETCODE varEventImplAdded(
   SCIP_VAR*             var,                /**< problem variable to change */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue          /**< event queue */
   )
{
   SCIP_EVENT* event;

   assert(var != NULL);

   /* issue IMPLADDED event on variable */
   SCIP_CALL( SCIPeventCreateImplAdded(&event, blkmem, var) );
   SCIP_CALL( SCIPeventqueueAdd(eventqueue, blkmem, set, NULL, NULL, NULL, NULL, &event) );

   return SCIP_OKAY;
}

/** actually performs the addition of a variable bound to the variable's vbound arrays */
static
SCIP_RETCODE varAddVbound(
   SCIP_VAR*             var,                /**< problem variable x in x <= b*z + d  or  x >= b*z + d */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_BOUNDTYPE        vbtype,             /**< type of variable bound (LOWER or UPPER) */
   SCIP_VAR*             vbvar,              /**< variable z    in x <= b*z + d  or  x >= b*z + d */
   SCIP_Real             vbcoef,             /**< coefficient b in x <= b*z + d  or  x >= b*z + d */
   SCIP_Real             vbconstant          /**< constant d    in x <= b*z + d  or  x >= b*z + d */
   )
{
   SCIP_Bool added;

   SCIPdebugMessage("adding variable bound: <%s> %s %g<%s> %+g\n", 
      SCIPvarGetName(var), vbtype == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=", vbcoef, SCIPvarGetName(vbvar), vbconstant);

   /* check variable bound on debugging solution */
   SCIP_CALL( SCIPdebugCheckVbound(set, var, vbtype, vbvar, vbcoef, vbconstant) ); /*lint !e506 !e774*/

   /* perform the addition */
   if( vbtype == SCIP_BOUNDTYPE_LOWER )
   {
      SCIP_CALL( SCIPvboundsAdd(&var->vlbs, blkmem, set, vbtype, vbvar, vbcoef, vbconstant, &added) );
   }
   else
   {
      SCIP_CALL( SCIPvboundsAdd(&var->vubs, blkmem, set, vbtype, vbvar, vbcoef, vbconstant, &added) );
   }

   if( added )
   {
      /* issue IMPLADDED event */
      SCIP_CALL( varEventImplAdded(var, blkmem, set, eventqueue) );
   }

   return SCIP_OKAY;
}

/** checks whether the given implication is redundant or infeasible w.r.t. the implied variables global bounds */
static
void checkImplic(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR*             implvar,            /**< variable y in implication y <= b or y >= b */
   SCIP_BOUNDTYPE        impltype,           /**< type       of implication y <= b (SCIP_BOUNDTYPE_UPPER) or y >= b (SCIP_BOUNDTYPE_LOWER) */
   SCIP_Real             implbound,          /**< bound b    in implication y <= b or y >= b */
   SCIP_Bool*            redundant,          /**< pointer to store whether the implication is redundant */
   SCIP_Bool*            infeasible          /**< pointer to store whether the implication is infeasible */
   )
{
   SCIP_Real impllb;
   SCIP_Real implub;

   assert(redundant != NULL);
   assert(infeasible != NULL);

   impllb = SCIPvarGetLbGlobal(implvar);
   implub = SCIPvarGetUbGlobal(implvar);
   if( impltype == SCIP_BOUNDTYPE_LOWER )
   {
      *infeasible = SCIPsetIsFeasGT(set, implbound, implub);
      *redundant = SCIPsetIsFeasLE(set, implbound, impllb);
   }
   else
   {
      *infeasible = SCIPsetIsFeasLT(set, implbound, impllb);
      *redundant = SCIPsetIsFeasGE(set, implbound, implub);
   }
}

/** applies the given implication, if it is not redundant */
static
SCIP_RETCODE applyImplic(
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             implvar,            /**< variable y in implication y <= b or y >= b */
   SCIP_BOUNDTYPE        impltype,           /**< type       of implication y <= b (SCIP_BOUNDTYPE_UPPER) or y >= b (SCIP_BOUNDTYPE_LOWER) */
   SCIP_Real             implbound,          /**< bound b    in implication y <= b or y >= b */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to count the number of performed bound changes, or NULL */
   )
{
   SCIP_Real implub;
   SCIP_Real impllb;
      
   assert(infeasible != NULL);
   
   *infeasible = FALSE;

   implub = SCIPvarGetUbGlobal(implvar);
   impllb = SCIPvarGetLbGlobal(implvar);
   if( impltype == SCIP_BOUNDTYPE_LOWER )
   {
      if( SCIPsetIsFeasGT(set, implbound, implub) )
      {
         /* the implication produces a conflict: the problem is infeasible */
         *infeasible = TRUE;
      }
      else if( SCIPsetIsFeasGT(set, implbound, impllb) )
      {
         SCIP_CALL( SCIPvarChgLbGlobal(implvar, blkmem, set, stat, lp, branchcand, eventqueue, implbound) );
         if( nbdchgs != NULL )
            (*nbdchgs)++;
      }
   }
   else
   {
      if( SCIPsetIsFeasLT(set, implbound, impllb) )
      {
         /* the implication produces a conflict: the problem is infeasible */
         *infeasible = TRUE;
      }
      else if( SCIPsetIsFeasLT(set, implbound, implub) )
      {
         SCIP_CALL( SCIPvarChgUbGlobal(implvar, blkmem, set, stat, lp, branchcand, eventqueue, implbound) );
         if( nbdchgs != NULL )
            (*nbdchgs)++;
      }
   }

   return SCIP_OKAY;
}

/** actually performs the addition of an implication to the variable's implication arrays,
 *  and adds the corresponding implication or variable bound to the implied variable;
 *  if the implication is conflicting, the variable is fixed to the opposite value;
 *  if the variable is already fixed to the given value, the implication is performed immediately;
 *  if the implication is redundant with respect to the variables' global bounds, it is ignored
 */
static
SCIP_RETCODE varAddImplic(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Bool             varfixing,          /**< FALSE if y should be added in implications for x == 0, TRUE for x == 1 */
   SCIP_VAR*             implvar,            /**< variable y in implication y <= b or y >= b */
   SCIP_BOUNDTYPE        impltype,           /**< type       of implication y <= b (SCIP_BOUNDTYPE_UPPER) or y >= b (SCIP_BOUNDTYPE_LOWER) */
   SCIP_Real             implbound,          /**< bound b    in implication y <= b or y >= b */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs,            /**< pointer to count the number of performed bound changes, or NULL */
   SCIP_Bool*            added               /**< pointer to store whether an implication was added */
   )
{
   SCIP_Bool redundant;
   SCIP_Bool conflict;

   assert(var != NULL);
   assert(SCIPvarIsActive(var));
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);
   assert(SCIPvarIsActive(implvar) || SCIPvarGetStatus(implvar) == SCIP_VARSTATUS_FIXED);
   assert(infeasible != NULL);
   assert(added != NULL);

   /* check implication on debugging solution */
   SCIP_CALL( SCIPdebugCheckImplic(set, var, varfixing, implvar, impltype, implbound) ); /*lint !e506 !e774*/

   *infeasible = FALSE;
   *added = FALSE;

   /* check, if the implication is redundant or infeasible */
   checkImplic(set, implvar, impltype, implbound, &redundant, &conflict);
   assert(!redundant || !conflict);
   if( redundant )
      return SCIP_OKAY;

   if( var == implvar )
   {
      /* variable implies itself: x == varfixing  =>  x == (impltype == SCIP_BOUNDTYPE_LOWER) */
      assert(SCIPsetIsEQ(set, implbound, 0.0) || SCIPsetIsEQ(set, implbound, 1.0));
      assert(SCIPsetIsEQ(set, implbound, 0.0) == (impltype == SCIP_BOUNDTYPE_UPPER));
      assert(SCIPsetIsEQ(set, implbound, 1.0) == (impltype == SCIP_BOUNDTYPE_LOWER));
      *infeasible = ((varfixing == TRUE) == (impltype == SCIP_BOUNDTYPE_UPPER));
      return SCIP_OKAY;
   }

   /* check, if the variable is already fixed */
   if( SCIPvarGetLbGlobal(var) > 0.5 || SCIPvarGetUbGlobal(var) < 0.5 )
   {
      /* if the variable is fixed to the given value, perform the implication; otherwise, ignore the implication */
      if( varfixing == (SCIPvarGetLbGlobal(var) > 0.5) )
      {
         SCIP_CALL( applyImplic(blkmem, set, stat, lp, branchcand, eventqueue, 
               implvar, impltype, implbound, infeasible, nbdchgs) );
      }
      return SCIP_OKAY;
   }

   assert((impltype == SCIP_BOUNDTYPE_LOWER && SCIPsetIsGT(set, implbound, SCIPvarGetLbGlobal(implvar)))
      || (impltype == SCIP_BOUNDTYPE_UPPER && SCIPsetIsLT(set, implbound, SCIPvarGetUbGlobal(implvar))));

   if( !conflict )
   {
      assert(SCIPvarIsActive(implvar)); /* a fixed implvar would either cause a redundancy or infeasibility */

      /* add implication x == 0/1 -> y <= b / y >= b to the implications list of x */
      SCIPdebugMessage("adding implication: <%s> == %d  ==>  <%s> %s %g\n", 
         SCIPvarGetName(var), varfixing,
         SCIPvarGetName(implvar), impltype == SCIP_BOUNDTYPE_UPPER ? "<=" : ">=", implbound);
      SCIP_CALL( SCIPimplicsAdd(&var->implics, blkmem, set, stat, varfixing, implvar, impltype, implbound,
            &conflict, added) );
   }
   assert(!conflict || !(*added));

   /* on conflict, fix the variable to the opposite value */
   if( conflict )
   {
      SCIPdebugMessage(" -> implication yields a conflict: fix <%s> == %d\n", SCIPvarGetName(var), !varfixing);

      if( varfixing )
      {
         SCIP_CALL( SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, 0.0) );
      }
      else
      {
         SCIP_CALL( SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, 1.0) );
      }
      if( nbdchgs != NULL )
         (*nbdchgs)++;

      return SCIP_OKAY;
   }
   else if( *added )
   {
      /* issue IMPLADDED event */
      SCIP_CALL( varEventImplAdded(var, blkmem, set, eventqueue) );
   }
   else
   {
      /* the implication was redundant: the inverse is also redundant */
      return SCIP_OKAY;
   }

   assert(SCIPvarIsActive(implvar)); /* a fixed implvar would either cause a redundancy or infeasibility */

   /* check, whether implied variable is binary */
   if( SCIPvarGetType(implvar) == SCIP_VARTYPE_BINARY )
   {
      SCIP_Bool inverseadded;

      assert(SCIPsetIsEQ(set, implbound, 0.0) == (impltype == SCIP_BOUNDTYPE_UPPER));
      assert(SCIPsetIsEQ(set, implbound, 1.0) == (impltype == SCIP_BOUNDTYPE_LOWER));

      /* add inverse implication y == 0/1 -> x == 0/1 to the implications list of y:
       *   x == 0 -> y == 0  <->  y == 1 -> x == 1
       *   x == 1 -> y == 0  <->  y == 1 -> x == 0
       *   x == 0 -> y == 1  <->  y == 0 -> x == 1
       *   x == 1 -> y == 1  <->  y == 0 -> x == 0
       */
      SCIPdebugMessage("adding inverse implication: <%s> == %d  ==>  <%s> == %d\n",
         SCIPvarGetName(implvar), (impltype == SCIP_BOUNDTYPE_UPPER), SCIPvarGetName(var), !varfixing);
      SCIP_CALL( SCIPimplicsAdd(&implvar->implics, blkmem, set, stat, 
            (impltype == SCIP_BOUNDTYPE_UPPER), var, varfixing ? SCIP_BOUNDTYPE_UPPER : SCIP_BOUNDTYPE_LOWER, 
            varfixing ? 0.0 : 1.0, &conflict, &inverseadded) );
      assert(inverseadded == !conflict); /* if there is no conflict, the implication must not be redundant */

      /* on conflict, fix the variable to the opposite value */
      if( conflict )
      {
         SCIPdebugMessage(" -> implication yields a conflict: fix <%s> == %d\n", 
            SCIPvarGetName(implvar), impltype == SCIP_BOUNDTYPE_LOWER);

         /* remove the original implication (which is now redundant due to the fixing of the implvar) */
         SCIPdebugMessage(" -> deleting implication: <%s> == %d  ==>  <%s> %s %g\n", 
            SCIPvarGetName(var), varfixing, SCIPvarGetName(implvar), impltype == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            implbound);
         SCIP_CALL( SCIPimplicsDel(&var->implics, blkmem, set, varfixing, implvar, impltype) );

         /* fix implvar */
         if( impltype == SCIP_BOUNDTYPE_UPPER )
         {
            SCIP_CALL( SCIPvarChgUbGlobal(implvar, blkmem, set, stat, lp, branchcand, eventqueue, 0.0) );
         }
         else
         {
            SCIP_CALL( SCIPvarChgLbGlobal(implvar, blkmem, set, stat, lp, branchcand, eventqueue, 1.0) );
         }
         if( nbdchgs != NULL )
            (*nbdchgs)++;
      }
      else if( inverseadded )
      {
         /* issue IMPLADDED event */
         SCIP_CALL( varEventImplAdded(implvar, blkmem, set, eventqueue) );
      }
   }
   else
   {
      SCIP_Real lb;
      SCIP_Real ub;

      /* add inverse variable bound to the variable bounds of y with global bounds y \in [lb,ub]:
       *   x == 0 -> y <= b  <->  y <= (ub - b)*x + b
       *   x == 1 -> y <= b  <->  y <= (b - ub)*x + ub
       *   x == 0 -> y >= b  <->  y >= (lb - b)*x + b
       *   x == 1 -> y >= b  <->  y >= (b - lb)*x + lb
       */
      lb = SCIPvarGetLbGlobal(implvar);
      ub = SCIPvarGetUbGlobal(implvar);
      if( impltype == SCIP_BOUNDTYPE_UPPER )
      {
         SCIP_CALL( varAddVbound(implvar, blkmem, set, eventqueue, SCIP_BOUNDTYPE_UPPER, var,
               varfixing ? implbound - ub : ub - implbound, varfixing ? ub : implbound) );
      }
      else
      {
         SCIP_CALL( varAddVbound(implvar, blkmem, set, eventqueue, SCIP_BOUNDTYPE_LOWER, var,
               varfixing ? implbound - lb : lb - implbound, varfixing ? lb : implbound) );
      }
   }

   return SCIP_OKAY;
}

/** adds given implication to the variable's implication list, and adds all implications directly implied by this
 *  implication to the variable's implication list;
 *  if the implication is conflicting, the variable is fixed to the opposite value;
 *  if the variable is already fixed to the given value, the implication is performed immediately;
 *  if the implication is redundant with respect to the variables' global bounds, it is ignored
 */
static
SCIP_RETCODE varAddTransitiveImplic(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Bool             varfixing,          /**< FALSE if y should be added in implications for x == 0, TRUE for x == 1 */
   SCIP_VAR*             implvar,            /**< variable y in implication y <= b or y >= b */
   SCIP_BOUNDTYPE        impltype,           /**< type       of implication y <= b (SCIP_BOUNDTYPE_UPPER) or y >= b (SCIP_BOUNDTYPE_LOWER) */
   SCIP_Real             implbound,          /**< bound b    in implication y <= b or y >= b */
   SCIP_Bool             transitive,         /**< should transitive closure of implication also be added? */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to count the number of performed bound changes, or NULL */
   )
{
   SCIP_Bool added;

   assert(var != NULL);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);
   assert(SCIPvarIsActive(var));
   assert(implvar != NULL);
   assert(SCIPvarIsActive(implvar) || SCIPvarGetStatus(implvar) == SCIP_VARSTATUS_FIXED);
   assert(infeasible != NULL);

   /* add implication x == varfixing -> y <= b / y >= b to the implications list of x */
   SCIP_CALL( varAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
         varfixing, implvar, impltype, implbound, infeasible, nbdchgs, &added) );
   if( *infeasible || var == implvar || !transitive || !added )
      return SCIP_OKAY;

   assert(SCIPvarIsActive(implvar)); /* a fixed implvar would either cause a redundancy or infeasibility */

   /* add transitive closure */
   if( SCIPvarGetType(implvar) == SCIP_VARTYPE_BINARY )
   {
      SCIP_VAR** implvars;
      SCIP_BOUNDTYPE* impltypes;
      SCIP_Real* implbounds;
      int nimpls;
#if 0
      SCIP_CLIQUE** cliques;
      int ncliques;
#endif
      SCIP_Bool implvarfixing;
      int i;

      implvarfixing = (impltype == SCIP_BOUNDTYPE_LOWER);

      /* binary variable: implications of implvar */
      nimpls = SCIPimplicsGetNImpls(implvar->implics, implvarfixing);
      implvars = SCIPimplicsGetVars(implvar->implics, implvarfixing);
      impltypes = SCIPimplicsGetTypes(implvar->implics, implvarfixing);
      implbounds = SCIPimplicsGetBounds(implvar->implics, implvarfixing);
      /* we have to iterate from back to front, because in varAddImplic() it may happen that a conflict is detected and
       * implvars[i] is fixed, s.t. the implication y == varfixing -> z <= b / z >= b is deleted; this affects the
       * array over which we currently iterate; the only thing that can happen, is that elements of the array are
       * deleted; in this case, the subsequent elements are moved to the front; if we iterate from back to front, the
       * only thing that can happen is that we add the same implication twice - this does no harm
       */
      for( i = nimpls-1; i >= 0 && !(*infeasible); --i )
      {
         assert(implvars[i] != implvar);

         /* we have x == varfixing -> y == implvarfixing -> z <= b / z >= b:
          * add implication x == varfixing -> z <= b / z >= b to the implications list of x 
          */
         if( SCIPvarIsActive(implvars[i]) )
         {
            SCIP_CALL( varAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
                  varfixing, implvars[i], impltypes[i], implbounds[i], infeasible, nbdchgs, &added) );
            assert(SCIPimplicsGetNImpls(implvar->implics, implvarfixing) <= nimpls);
            nimpls = SCIPimplicsGetNImpls(implvar->implics, implvarfixing);
            i = MIN(i, nimpls); /* some elements from the array could have been removed */
         }
      }

#if 0 /* this may produce way too many cliques in the clique table - and they are unnecessary */
      /* binary variable: cliques of implvar */
      ncliques = SCIPcliquelistGetNCliques(implvar->cliquelist, implvarfixing);
      cliques = SCIPcliquelistGetCliques(implvar->cliquelist, implvarfixing);
      for( i = 0; i < ncliques && !(*infeasible); ++i )
      {
         SCIP_VAR** cliquevars;
         SCIP_Bool* cliquevalues;
         SCIP_VAR** newcliquevars;
         SCIP_Bool* newcliquevalues;
         int ncliquevars;
         int implvarpos;

         /* we have x == varfixing -> y == implvarfixing -> z[j] == !cliquevalues[j] for all j in clique:
          * add copy of clique with y == implvarfixing replaced by x == varfixing
          */
         ncliquevars = SCIPcliqueGetNVars(cliques[i]);
         cliquevars = SCIPcliqueGetVars(cliques[i]);
         cliquevalues = SCIPcliqueGetValues(cliques[i]);
         implvarpos = SCIPcliqueSearchVar(cliques[i], implvar, implvarfixing);
         assert(0 <= implvarpos && implvarpos < ncliquevars);
         assert(cliquevars[implvarpos] == implvar);
         assert(cliquevalues[implvarpos] == implvarfixing);

         /* allocate temporary memory for copy of clique */
         SCIP_CALL( SCIPsetDuplicateBufferArray(set, &newcliquevars, cliquevars, ncliquevars) );
         SCIP_CALL( SCIPsetDuplicateBufferArray(set, &newcliquevalues, cliquevalues, ncliquevars) );

         /* replace y == implvarfixing with x == varfixing */
         newcliquevars[implvarpos] = var;
         newcliquevalues[implvarpos] = varfixing;

         /* add clique */
         SCIP_CALL( SCIPcliquetableAdd(cliquetable, blkmem, set, stat, lp, branchcand, eventqueue,
               newcliquevars, newcliquevalues, ncliquevars, infeasible, NULL) );

         /* free temporary memory */
         SCIPsetFreeBufferArray(set, &newcliquevalues);
         SCIPsetFreeBufferArray(set, &newcliquevars);
      }
#endif
   }
   else
   {
      /* non-binary variable: variable lower bounds of implvar */
      if( impltype == SCIP_BOUNDTYPE_UPPER && implvar->vlbs != NULL )
      {
         SCIP_VAR** vlbvars;
         SCIP_Real* vlbcoefs;
         SCIP_Real* vlbconstants;
         int nvlbvars;
         int i;

         nvlbvars = SCIPvboundsGetNVbds(implvar->vlbs);
         vlbvars = SCIPvboundsGetVars(implvar->vlbs);
         vlbcoefs = SCIPvboundsGetCoefs(implvar->vlbs);
         vlbconstants = SCIPvboundsGetConstants(implvar->vlbs);
         for( i = 0; i < nvlbvars && !(*infeasible); ++i )
         {
            assert(vlbvars[i] != implvar);
            assert(!SCIPsetIsZero(set, vlbcoefs[i]));

            /* we have x == varfixing -> y <= b and y >= c*z + d:
             *   c > 0: add implication x == varfixing -> z <= (b-d)/c to the implications list of x
             *   c < 0: add implication x == varfixing -> z >= (b-d)/c to the implications list of x
             */
            if( SCIPvarIsActive(vlbvars[i]) )
            {
               SCIP_Real vbimplbound;

               vbimplbound = (implbound - vlbconstants[i])/vlbcoefs[i];
               if( vlbcoefs[i] >= 0.0 )
               {
                  vbimplbound = adjustedUb(set, SCIPvarGetType(vlbvars[i]), vbimplbound);
                  SCIP_CALL( varAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
                        varfixing, vlbvars[i], SCIP_BOUNDTYPE_UPPER, vbimplbound, infeasible, nbdchgs, &added) );
               }
               else
               {
                  vbimplbound = adjustedLb(set, SCIPvarGetType(vlbvars[i]), vbimplbound);
                  SCIP_CALL( varAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
                        varfixing, vlbvars[i], SCIP_BOUNDTYPE_LOWER, vbimplbound, infeasible, nbdchgs, &added) );
               }
               assert(SCIPvboundsGetNVbds(implvar->vlbs) == nvlbvars);
            }
         }
      }

      /* non-binary variable: variable upper bounds of implvar */
      if( impltype == SCIP_BOUNDTYPE_LOWER && implvar->vubs != NULL )
      {
         SCIP_VAR** vubvars;
         SCIP_Real* vubcoefs;
         SCIP_Real* vubconstants;
         int nvubvars;
         int i;

         nvubvars = SCIPvboundsGetNVbds(implvar->vubs);
         vubvars = SCIPvboundsGetVars(implvar->vubs);
         vubcoefs = SCIPvboundsGetCoefs(implvar->vubs);
         vubconstants = SCIPvboundsGetConstants(implvar->vubs);
         for( i = 0; i < nvubvars && !(*infeasible); ++i )
         {
            assert(vubvars[i] != implvar);
            assert(!SCIPsetIsZero(set, vubcoefs[i]));

            /* we have x == varfixing -> y >= b and y <= c*z + d:
             *   c > 0: add implication x == varfixing -> z >= (b-d)/c to the implications list of x
             *   c < 0: add implication x == varfixing -> z <= (b-d)/c to the implications list of x
             */
            if( SCIPvarIsActive(vubvars[i]) )
            {
               SCIP_Real vbimplbound;

               vbimplbound = (implbound - vubconstants[i])/vubcoefs[i];
               if( vubcoefs[i] >= 0.0 )
               {
                  vbimplbound = adjustedLb(set, SCIPvarGetType(vubvars[i]), vbimplbound);
                  SCIP_CALL( varAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
                        varfixing, vubvars[i], SCIP_BOUNDTYPE_LOWER, vbimplbound, infeasible, nbdchgs, &added) );
               }
               else
               {
                  vbimplbound = adjustedUb(set, SCIPvarGetType(vubvars[i]), vbimplbound);
                  SCIP_CALL( varAddImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
                        varfixing, vubvars[i], SCIP_BOUNDTYPE_UPPER, vbimplbound, infeasible, nbdchgs, &added) );
               }
               assert(SCIPvboundsGetNVbds(implvar->vubs) == nvubvars);
            }
         }
      }
   }

   return SCIP_OKAY;
}

/** informs variable x about a globally valid variable lower bound x >= b*z + d with integer variable z;
 *  if z is binary, the corresponding valid implication for z is also added;
 *  improves the global bounds of the variable and the vlb variable if possible
 */
SCIP_RETCODE SCIPvarAddVlb(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             vlbvar,             /**< variable z    in x >= b*z + d */
   SCIP_Real             vlbcoef,            /**< coefficient b in x >= b*z + d */
   SCIP_Real             vlbconstant,        /**< constant d    in x >= b*z + d */
   SCIP_Bool             transitive,         /**< should transitive closure of implication also be added? */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to store the number of performed bound changes, or NULL */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(vlbvar) != SCIP_VARTYPE_CONTINUOUS);
   assert(infeasible != NULL);

   SCIPdebugMessage("adding variable lower bound <%s> >= %g<%s> + %g\n", 
      SCIPvarGetName(var), vlbcoef, SCIPvarGetName(vlbvar), vlbconstant);

   *infeasible = FALSE;
   if( nbdchgs != NULL )
      *nbdchgs = 0;

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      SCIP_CALL( SCIPvarAddVlb(var->data.original.transvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
            vlbvar, vlbcoef, vlbconstant, transitive, infeasible, nbdchgs) );
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      /* transform b*z + d into the corresponding sum after transforming z to an active problem variable */
      SCIP_CALL( SCIPvarGetProbvarSum(&vlbvar, &vlbcoef, &vlbconstant) );
      SCIPdebugMessage(" -> transformed to variable lower bound <%s> >= %g<%s> + %g\n", 
         SCIPvarGetName(var), vlbcoef, SCIPvarGetName(vlbvar), vlbconstant);

      /* if the vlb coefficient is zero, just update the lower bound of the variable */
      if( SCIPsetIsZero(set, vlbcoef) )
      {
         if( SCIPsetIsFeasGT(set, vlbconstant, SCIPvarGetUbGlobal(var)) )
            *infeasible = TRUE;
         else if( SCIPsetIsFeasGT(set, vlbconstant, SCIPvarGetLbGlobal(var)) )
         {
            SCIP_CALL( SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, vlbconstant) );
            if( nbdchgs != NULL )
               (*nbdchgs)++;
         }
      }
      else if( SCIPvarIsActive(vlbvar) )
      {
         SCIP_Real xlb;
         SCIP_Real xub;
         SCIP_Real zlb;
         SCIP_Real zub;
         SCIP_Real minvlb;
         SCIP_Real maxvlb;

         assert(SCIPvarGetStatus(vlbvar) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(vlbvar) == SCIP_VARSTATUS_COLUMN);
         assert(vlbcoef != 0.0);

         xlb = SCIPvarGetLbGlobal(var);
         xub = SCIPvarGetUbGlobal(var);
         zlb = SCIPvarGetLbGlobal(vlbvar);
         zub = SCIPvarGetUbGlobal(vlbvar);

         /* improve global bounds of vlb variable, and calculate minimal and maximal value of variable bound */
         if( vlbcoef >= 0.0 )
         {
            SCIP_Real newzub;

            /* x >= b*z + d  ->  z <= (x-d)/b */
            newzub = (xub - vlbconstant)/vlbcoef;
            if( SCIPsetIsFeasLT(set, newzub, zlb) )
            {
               *infeasible = TRUE;
               return SCIP_OKAY;
            }
            if( SCIPsetIsFeasLT(set, newzub, zub) )
            {
               SCIP_CALL( SCIPvarChgUbGlobal(vlbvar, blkmem, set, stat, lp, branchcand, eventqueue, newzub) );
               zub = SCIPvarGetUbGlobal(vlbvar); /* bound might have been adjusted due to integrality condition */
               if( nbdchgs != NULL )
                  (*nbdchgs)++;
            }
            minvlb = vlbcoef * zlb + vlbconstant;
            maxvlb = vlbcoef * zub + vlbconstant;
         }
         else
         {
            SCIP_Real newzlb;

            /* x >= b*z + d  ->  z >= (x-d)/b */
            newzlb = (xub - vlbconstant)/vlbcoef;
            if( SCIPsetIsFeasGT(set, newzlb, zub) )
            {
               *infeasible = TRUE;
               return SCIP_OKAY;
            }
            if( SCIPsetIsFeasGT(set, newzlb, zlb) )
            {
               SCIP_CALL( SCIPvarChgLbGlobal(vlbvar, blkmem, set, stat, lp, branchcand, eventqueue, newzlb) );
               zlb = SCIPvarGetLbGlobal(vlbvar); /* bound might have been adjusted due to integrality condition */
               if( nbdchgs != NULL )
                  (*nbdchgs)++;
            }
            minvlb = vlbcoef * zub + vlbconstant;
            maxvlb = vlbcoef * zlb + vlbconstant;
         }
         assert(minvlb <= maxvlb);

         /* adjust bounds due to integrality of variable */
         minvlb = adjustedLb(set, SCIPvarGetType(var), minvlb);
         maxvlb = adjustedLb(set, SCIPvarGetType(var), maxvlb);

         /* improve global lower bound of variable */
         if( SCIPsetIsFeasGT(set, minvlb, xub) )
         {
            *infeasible = TRUE;
            return SCIP_OKAY;
         }
         if( SCIPsetIsFeasGT(set, minvlb, xlb) )
         {
            SCIP_CALL( SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, minvlb) );
            xlb = SCIPvarGetLbGlobal(var); /* bound might have been adjusted due to integrality condition */
            if( nbdchgs != NULL )
               (*nbdchgs)++;
         }
         minvlb = xlb;

         /* improve variable bound for binary z by moving the variable's global bound to the vlb constant */
         if( SCIPvarGetType(vlbvar) == SCIP_VARTYPE_BINARY )
         {
            /* b > 0: x >= (maxvlb - minvlb) * z + minvlb
             * b < 0: x >= (minvlb - maxvlb) * z + maxvlb
             */
            if( vlbcoef >= 0.0 )
            {
               vlbcoef = maxvlb - minvlb;
               vlbconstant = minvlb;
            }
            else
            {
               vlbcoef = minvlb - maxvlb;
               vlbconstant = maxvlb;
            }
         }

         /* add variable bound to the variable bounds list */
         if( SCIPsetIsFeasGT(set, maxvlb, xlb) )
         {
            assert(SCIPvarGetStatus(var) != SCIP_VARSTATUS_FIXED);

            /* if the vlb variable is binary, add the corresponding implication to the vlb variable's implication
             * list, thereby also adding the variable bound itself
             */
            if( SCIPvarGetType(vlbvar) == SCIP_VARTYPE_BINARY )
            {
               /* add corresponding implication:
                *   b > 0, x >= b*z + d  <->  z == 1 -> x >= b+d
                *   b < 0, x >= b*z + d  <->  z == 0 -> x >= d
                */
               SCIP_CALL( varAddTransitiveImplic(vlbvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
                     (vlbcoef >= 0.0), var, SCIP_BOUNDTYPE_LOWER, maxvlb, transitive, infeasible, nbdchgs) );
            }
            else
            {
               SCIP_CALL( varAddVbound(var, blkmem, set, eventqueue, 
                     SCIP_BOUNDTYPE_LOWER, vlbvar, vlbcoef, vlbconstant) );
            }
         }
      }
      break;
      
   case SCIP_VARSTATUS_AGGREGATED:
      /* x = a*y + c:  x >= b*z + d  <=>  a*y + c >= b*z + d  <=>  y >= b/a * z + (d-c)/a, if a > 0
       *                                                           y <= b/a * z + (d-c)/a, if a < 0
       */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> add variable lower bound */
         SCIP_CALL( SCIPvarAddVlb(var->data.aggregate.var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
               vlbvar, vlbcoef/var->data.aggregate.scalar,
               (vlbconstant - var->data.aggregate.constant)/var->data.aggregate.scalar, transitive, infeasible, nbdchgs) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> add variable upper bound */
         SCIP_CALL( SCIPvarAddVub(var->data.aggregate.var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
               vlbvar, vlbcoef/var->data.aggregate.scalar,
               (vlbconstant - var->data.aggregate.constant)/var->data.aggregate.scalar, transitive, infeasible, nbdchgs) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
      
   case SCIP_VARSTATUS_MULTAGGR:
      /* nothing to do here */
      break;
      
   case SCIP_VARSTATUS_NEGATED:
      /* x = offset - x':  x >= b*z + d  <=>  offset - x' >= b*z + d  <=>  x' <= -b*z + (offset-d) */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarAddVub(var->negatedvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
            vlbvar, -vlbcoef, var->data.negate.constant - vlbconstant, transitive, infeasible, nbdchgs) );
      break;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** informs variable x about a globally valid variable upper bound x <= b*z + d with integer variable z;
 *  if z is binary, the corresponding valid implication for z is also added;
 *  updates the global bounds of the variable and the vub variable correspondingly
 */
SCIP_RETCODE SCIPvarAddVub(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_VAR*             vubvar,             /**< variable z    in x <= b*z + d */
   SCIP_Real             vubcoef,            /**< coefficient b in x <= b*z + d */
   SCIP_Real             vubconstant,        /**< constant d    in x <= b*z + d */
   SCIP_Bool             transitive,         /**< should transitive closure of implication also be added? */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to store the number of performed bound changes, or NULL */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(vubvar) != SCIP_VARTYPE_CONTINUOUS);
   assert(infeasible != NULL);

   SCIPdebugMessage("adding variable upper bound <%s> <= %g<%s> + %g\n", 
      SCIPvarGetName(var), vubcoef, SCIPvarGetName(vubvar), vubconstant);

   *infeasible = FALSE;
   if( nbdchgs != NULL )
      *nbdchgs = 0;

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      SCIP_CALL( SCIPvarAddVub(var->data.original.transvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
            vubvar, vubcoef, vubconstant, transitive, infeasible, nbdchgs) );
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      /* transform b*z + d into the corresponding sum after transforming z to an active problem variable */
      SCIP_CALL( SCIPvarGetProbvarSum(&vubvar, &vubcoef, &vubconstant) );
      SCIPdebugMessage(" -> transformed to variable upper bound <%s> <= %g<%s> + %g\n", 
         SCIPvarGetName(var), vubcoef, SCIPvarGetName(vubvar), vubconstant);

      /* if the vub coefficient is zero, just update the upper bound of the variable */
      if( SCIPsetIsZero(set, vubcoef) )
      {
         if( SCIPsetIsFeasLT(set, vubconstant, SCIPvarGetLbGlobal(var)) )
            *infeasible = TRUE;
         else if( SCIPsetIsFeasLT(set, vubconstant, SCIPvarGetUbGlobal(var)) )
         {
            SCIP_CALL( SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, vubconstant) );
            if( nbdchgs != NULL )
               (*nbdchgs)++;
         }
      }
      else if( SCIPvarIsActive(vubvar) )
      {
         SCIP_Real xlb;
         SCIP_Real xub;
         SCIP_Real zlb;
         SCIP_Real zub;
         SCIP_Real minvub;
         SCIP_Real maxvub;

         assert(SCIPvarGetStatus(vubvar) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(vubvar) == SCIP_VARSTATUS_COLUMN);
         assert(vubcoef != 0.0);

         xlb = SCIPvarGetLbGlobal(var);
         xub = SCIPvarGetUbGlobal(var);
         zlb = SCIPvarGetLbGlobal(vubvar);
         zub = SCIPvarGetUbGlobal(vubvar);

         /* improve global bounds of vub variable, and calculate minimal and maximal value of variable bound */
         if( vubcoef >= 0.0 )
         {
            SCIP_Real newzlb;

            /* x <= b*z + d  ->  z >= (x-d)/b */
            newzlb = (xlb - vubconstant)/vubcoef;
            if( SCIPsetIsFeasGT(set, newzlb, zub) )
            {
               *infeasible = TRUE;
               return SCIP_OKAY;
            }
            if( SCIPsetIsFeasGT(set, newzlb, zlb) )
            {
               SCIP_CALL( SCIPvarChgLbGlobal(vubvar, blkmem, set, stat, lp, branchcand, eventqueue, newzlb) );
               zlb = SCIPvarGetLbGlobal(vubvar); /* bound might have been adjusted due to integrality condition */
            }
            minvub = vubcoef * zlb + vubconstant;
            maxvub = vubcoef * zub + vubconstant;
         }
         else
         {
            SCIP_Real newzub;

            /* x <= b*z + d  ->  z <= (x-d)/b */
            newzub = (xlb - vubconstant)/vubcoef;
            if( SCIPsetIsFeasLT(set, newzub, zlb) )
            {
               *infeasible = TRUE;
               return SCIP_OKAY;
            }
            if( SCIPsetIsFeasLT(set, newzub, zub) )
            {
               SCIP_CALL( SCIPvarChgUbGlobal(vubvar, blkmem, set, stat, lp, branchcand, eventqueue, newzub) );
               zub = SCIPvarGetUbGlobal(vubvar); /* bound might have been adjusted due to integrality condition */
            }
            minvub = vubcoef * zub + vubconstant;
            maxvub = vubcoef * zlb + vubconstant;
         }
         assert(minvub <= maxvub);

         /* adjust bounds due to integrality of vub variable */
         minvub = adjustedUb(set, SCIPvarGetType(var), minvub);
         maxvub = adjustedUb(set, SCIPvarGetType(var), maxvub);

         /* improve global upper bound of variable */
         if( SCIPsetIsFeasLT(set, maxvub, xlb) )
         {
            *infeasible = TRUE;
            return SCIP_OKAY;
         }
         if( SCIPsetIsFeasLT(set, maxvub, xub) )
         {
            SCIP_CALL( SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, maxvub) );
            xub = SCIPvarGetUbGlobal(var); /* bound might have been adjusted due to integrality condition */
         }
         maxvub = xub;

         /* improve variable bound for binary z by moving the variable's global bound to the vub constant */
         if( SCIPvarGetType(vubvar) == SCIP_VARTYPE_BINARY )
         {
            /* b > 0: x <= (maxvub - minvub) * z + minvub
             * b < 0: x <= (minvub - maxvub) * z + maxvub
             */
            if( vubcoef >= 0.0 )
            {
               vubcoef = maxvub - minvub;
               vubconstant = minvub;
            }
            else
            {
               vubcoef = minvub - maxvub;
               vubconstant = maxvub;
            }
         }

         /* add variable bound to the variable bounds list */
         if( SCIPsetIsFeasLT(set, minvub, xub) )
         {
            assert(SCIPvarGetStatus(var) != SCIP_VARSTATUS_FIXED);

            /* if the vub variable is binary, add the corresponding implication to the vub variable's implication
             * list, thereby also adding the variable bound itself
             */
            if( SCIPvarGetType(vubvar) == SCIP_VARTYPE_BINARY )
            {
               /* add corresponding implication:
                *   b > 0, x <= b*z + d  <->  z == 0 -> x <= d
                *   b < 0, x <= b*z + d  <->  z == 1 -> x <= b+d
                */
               SCIP_CALL( varAddTransitiveImplic(vubvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
                     (vubcoef < 0.0), var, SCIP_BOUNDTYPE_UPPER, minvub, transitive, infeasible, nbdchgs) );
            }
            else
            {
               SCIP_CALL( varAddVbound(var, blkmem, set, eventqueue, 
                     SCIP_BOUNDTYPE_UPPER, vubvar, vubcoef, vubconstant) );
            }
         }
      }
      break;
      
   case SCIP_VARSTATUS_AGGREGATED:
      /* x = a*y + c:  x <= b*z + d  <=>  a*y + c <= b*z + d  <=>  y <= b/a * z + (d-c)/a, if a > 0
       *                                                           y >= b/a * z + (d-c)/a, if a < 0
       */
      assert(var->data.aggregate.var != NULL);
      if( SCIPsetIsPositive(set, var->data.aggregate.scalar) )
      {
         /* a > 0 -> add variable upper bound */
         SCIP_CALL( SCIPvarAddVub(var->data.aggregate.var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
               vubvar, vubcoef/var->data.aggregate.scalar,
               (vubconstant - var->data.aggregate.constant)/var->data.aggregate.scalar, transitive, infeasible, nbdchgs) );
      }
      else if( SCIPsetIsNegative(set, var->data.aggregate.scalar) )
      {
         /* a < 0 -> add variable lower bound */
         SCIP_CALL( SCIPvarAddVlb(var->data.aggregate.var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
               vubvar, vubcoef/var->data.aggregate.scalar,
               (vubconstant - var->data.aggregate.constant)/var->data.aggregate.scalar, transitive, infeasible, nbdchgs) );
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         return SCIP_INVALIDDATA;
      }
      break;
      
   case SCIP_VARSTATUS_MULTAGGR:
      /* nothing to do here */
      break;
      
   case SCIP_VARSTATUS_NEGATED:
      /* x = offset - x':  x <= b*z + d  <=>  offset - x' <= b*z + d  <=>  x' >= -b*z + (offset-d) */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarAddVlb(var->negatedvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
            vubvar, -vubcoef, var->data.negate.constant - vubconstant, transitive, infeasible, nbdchgs) );
      break;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** informs binary variable x about a globally valid implication:  x == 0 or x == 1  ==>  y <= b  or  y >= b;
 *  also adds the corresponding implication or variable bound to the implied variable;
 *  if the implication is conflicting, the variable is fixed to the opposite value;
 *  if the variable is already fixed to the given value, the implication is performed immediately;
 *  if the implication is redundant with respect to the variables' global bounds, it is ignored
 */
SCIP_RETCODE SCIPvarAddImplic(
   SCIP_VAR*             var,                /**< problem variable  */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Bool             varfixing,          /**< FALSE if y should be added in implications for x == 0, TRUE for x == 1 */
   SCIP_VAR*             implvar,            /**< variable y in implication y <= b or y >= b */
   SCIP_BOUNDTYPE        impltype,           /**< type       of implication y <= b (SCIP_BOUNDTYPE_UPPER) or y >= b (SCIP_BOUNDTYPE_LOWER) */
   SCIP_Real             implbound,          /**< bound b    in implication y <= b or y >= b */
   SCIP_Bool             transitive,         /**< should transitive closure of implication also be added? */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to store the number of performed bound changes, or NULL */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY); 
   assert(infeasible != NULL);

   *infeasible = FALSE;
   if( nbdchgs != NULL )
      *nbdchgs = 0;

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      SCIP_CALL( SCIPvarAddImplic(var->data.original.transvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
            varfixing, implvar, impltype, implbound, transitive, infeasible, nbdchgs) );
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      /* if the variable is fixed (although it has no FIXED status), and varfixing corresponds to the fixed value of
       * the variable, the implication can be applied directly;
       * otherwise, add implication to the implications list (and add inverse of implication to the implied variable)
       */
      if( SCIPvarGetLbGlobal(var) > 0.5 || SCIPvarGetUbGlobal(var) < 0.5 )
      {
         if( varfixing == (SCIPvarGetLbGlobal(var) > 0.5) )
         {
            SCIP_CALL( applyImplic(blkmem, set, stat, lp, branchcand, eventqueue,
                  implvar, impltype, implbound, infeasible, nbdchgs) );
         }
      }
      else
      {
         SCIP_CALL( SCIPvarGetProbvarBound(&implvar, &implbound, &impltype) );
         SCIPvarAdjustBd(implvar, set, impltype, &implbound);
         if( SCIPvarIsActive(implvar) || SCIPvarGetStatus(implvar) == SCIP_VARSTATUS_FIXED )
         {
            SCIP_CALL( varAddTransitiveImplic(var, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue,
                  varfixing, implvar, impltype, implbound, transitive, infeasible, nbdchgs) );
         }
      }
      break;
      
   case SCIP_VARSTATUS_FIXED:
      /* if varfixing corresponds to the fixed value of the variable, the implication can be applied directly */
      if( varfixing == (SCIPvarGetLbGlobal(var) > 0.5) )
      {
         SCIP_CALL( applyImplic(blkmem, set, stat, lp, branchcand, eventqueue,
               implvar, impltype, implbound, infeasible, nbdchgs) );
      }
      break;
      
   case SCIP_VARSTATUS_AGGREGATED:
      /* implication added for x == 1:
       *   x == 1 && x =  1*z + 0  ==>  y <= b or y >= b    <==>    z >= 1  ==>  y <= b or y >= b 
       *   x == 1 && x = -1*z + 1  ==>  y <= b or y >= b    <==>    z <= 0  ==>  y <= b or y >= b
       * implication added for x == 0:
       *   x == 0 && x =  1*z + 0  ==>  y <= b or y >= b    <==>    z <= 0  ==>  y <= b or y >= b
       *   x == 0 && x = -1*z + 1  ==>  y <= b or y >= b    <==>    z >= 1  ==>  y <= b or y >= b
       *
       * use only binary variables z 
       */
      assert(var->data.aggregate.var != NULL);
      if( SCIPvarGetType(var->data.aggregate.var) == SCIP_VARTYPE_BINARY )
      {   
         assert((var->data.aggregate.scalar == 1 && var->data.aggregate.constant == 0)
            || (var->data.aggregate.scalar == -1 && var->data.aggregate.constant == 1));
       
         if( var->data.aggregate.scalar > 0 )
         {
            SCIP_CALL( SCIPvarAddImplic(var->data.aggregate.var, blkmem, set, stat, lp, cliquetable, branchcand, 
                  eventqueue, varfixing, implvar, impltype, implbound, transitive, infeasible, nbdchgs) );
         }
         else
         {
            SCIP_CALL( SCIPvarAddImplic(var->data.aggregate.var, blkmem, set, stat, lp, cliquetable, branchcand, 
                  eventqueue, !varfixing, implvar, impltype, implbound, transitive, infeasible, nbdchgs) );
         }
      }
      break;
      
   case SCIP_VARSTATUS_MULTAGGR:
      /* nothing to do here */
      break;
      
   case SCIP_VARSTATUS_NEGATED:
      /* implication added for x == 1:
       *   x == 1 && x = -1*z + 1  ==>  y <= b or y >= b    <==>    z <= 0  ==>  y <= b or y >= b
       * implication added for x == 0:
       *   x == 0 && x = -1*z + 1  ==>  y <= b or y >= b    <==>    z >= 1  ==>  y <= b or y >= b
       */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      assert(SCIPvarGetType(var->negatedvar) == SCIP_VARTYPE_BINARY);

      SCIP_CALL( SCIPvarAddImplic(var->negatedvar, blkmem, set, stat, lp, cliquetable, branchcand, eventqueue, 
            !varfixing, implvar, impltype, implbound, transitive, infeasible, nbdchgs) );
      break;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** returns whether there is an implication x == varfixing -> y <= b or y >= b in the implication graph;
 *  implications that are represented as cliques in the clique table are not regarded (use SCIPvarsHaveCommonClique());
 *  both variables must be active, variable x must be binary
 */
SCIP_Bool SCIPvarHasImplic(
   SCIP_VAR*             var,                /**< problem variable x */
   SCIP_Bool             varfixing,          /**< FALSE if y should be searched in implications for x == 0, TRUE for x == 1 */
   SCIP_VAR*             implvar,            /**< variable y to search for */
   SCIP_BOUNDTYPE        impltype            /**< type of implication y <=/>= b to search for */
   )
{
   assert(var != NULL);
   assert(implvar != NULL);
   assert(SCIPvarIsActive(var));
   assert(SCIPvarIsActive(implvar));
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);

   return var->implics != NULL && SCIPimplicsContainsImpl(var->implics, varfixing, implvar, impltype);
}

/** returns whether there is an implication x == varfixing -> y == implvarfixing in the implication graph;
 *  implications that are represented as cliques in the clique table are not regarded (use SCIPvarsHaveCommonClique());
 *  both variables must be active binary variables
 */
SCIP_Bool SCIPvarHasBinaryImplic(
   SCIP_VAR*             var,                /**< problem variable x */
   SCIP_Bool             varfixing,          /**< FALSE if y should be searched in implications for x == 0, TRUE for x == 1 */
   SCIP_VAR*             implvar,            /**< variable y to search for */
   SCIP_Bool             implvarfixing       /**< value of the implied variable to search for */
   )
{
   assert(SCIPvarGetType(implvar) == SCIP_VARTYPE_BINARY);

   return SCIPvarHasImplic(var, varfixing, implvar, implvarfixing ? SCIP_BOUNDTYPE_LOWER : SCIP_BOUNDTYPE_UPPER);
}

/** fixes the bounds of a binary variable to the given value, counting bound changes and detecting infeasibility */
static
SCIP_RETCODE varFixBinary(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Bool             value,              /**< value to fix variable to */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to count the number of performed bound changes, or NULL */
   )
{
   assert(var != NULL);
   assert(infeasible != NULL);

   *infeasible = FALSE;

   if( value == FALSE )
   {
      if( var->glbdom.lb > 0.5 )
         *infeasible = TRUE;
      else if( var->glbdom.ub > 0.5 )
      {
         SCIP_CALL( SCIPvarChgUbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, 0.0) );
         if( nbdchgs != NULL )
            (*nbdchgs)++;
      }
   }
   else
   {
      if( var->glbdom.ub < 0.5 )
         *infeasible = TRUE;
      else if( var->glbdom.lb < 0.5 )
      {
         SCIP_CALL( SCIPvarChgLbGlobal(var, blkmem, set, stat, lp, branchcand, eventqueue, 1.0) );
         if( nbdchgs != NULL )
            (*nbdchgs)++;
      }
   }
   
   return SCIP_OKAY;
}

/** adds the variable to the given clique and updates the list of cliques the binary variable is member of;
 *  if the variable now appears twice in the clique with the same value, it is fixed to the opposite value;
 *  if the variable now appears twice in the clique with opposite values, all other variables are fixed to
 *  the opposite of the value they take in the clique
 */
SCIP_RETCODE SCIPvarAddClique(
   SCIP_VAR*             var,                /**< problem variable  */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_Bool             value,              /**< value of the variable in the clique */
   SCIP_CLIQUE*          clique,             /**< clique the variable should be added to */
   SCIP_Bool*            infeasible,         /**< pointer to store whether an infeasibility was detected */
   int*                  nbdchgs             /**< pointer to count the number of performed bound changes, or NULL */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY); 
   assert(infeasible != NULL);

   *infeasible = FALSE;

   /* get corresponding active problem variable */
   SCIP_CALL( SCIPvarGetProbvarBinary(&var, &value) );
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_FIXED
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);

   /* only column and loose variables may be member of a clique */
   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE )
   {
      SCIP_Bool doubleentry;
      SCIP_Bool oppositeentry;

      /* add variable to clique */
      SCIP_CALL( SCIPcliqueAddVar(clique, blkmem, set, var, value, &doubleentry, &oppositeentry) );

      /* add clique to variable's clique list */
      SCIP_CALL( SCIPcliquelistAdd(&var->cliquelist, blkmem, set, value, clique) );

      /* check consistency of cliquelist */
      SCIPcliquelistCheck(var->cliquelist, var);

      /* if the variable now appears twice with the same value in the clique, it can be fixed to the opposite value */
      if( doubleentry )
      {
         SCIP_CALL( varFixBinary(var, blkmem, set, stat, lp, branchcand, eventqueue, !value, infeasible, nbdchgs) );
      }

      /* if the variable appears with both values in the clique, all other variables of the clique can be fixed
       * to the opposite of the value they take in the clique
       */
      if( oppositeentry )
      {
         SCIP_VAR** vars;
         SCIP_Bool* values;
         int nvars;
         int i;

         nvars = SCIPcliqueGetNVars(clique);
         vars = SCIPcliqueGetVars(clique);
         values = SCIPcliqueGetValues(clique);
         for( i = 0; i < nvars && !(*infeasible); ++i )
         {
            if( vars[i] == var )
               continue;

            SCIP_CALL( varFixBinary(vars[i], blkmem, set, stat, lp, branchcand, eventqueue, !values[i], 
                  infeasible, nbdchgs) );
         }
      }
   }

   return SCIP_OKAY;
}

/** deletes the variable from the list of cliques the binary variable is member of, but does not change the clique
 *  itself
 */
SCIP_RETCODE SCIPvarDelCliqueFromList(
   SCIP_VAR*             var,                /**< problem variable  */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_Bool             value,              /**< value of the variable in the clique */
   SCIP_CLIQUE*          clique              /**< clique that should be removed from the variable's clique list */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY); 
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE);

   /* delete clique from variable's clique list */
   SCIP_CALL( SCIPcliquelistDel(&var->cliquelist, blkmem, value, clique) );

   return SCIP_OKAY;
}

/** deletes the variable from the given clique and updates the list of cliques the binary variable is member of */
SCIP_RETCODE SCIPvarDelClique(
   SCIP_VAR*             var,                /**< problem variable  */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_Bool             value,              /**< value of the variable in the clique */
   SCIP_CLIQUE*          clique              /**< clique the variable should be removed from */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY); 

   /* get corresponding active problem variable */
   SCIP_CALL( SCIPvarGetProbvarBinary(&var, &value) );
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_FIXED
      || SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);

   /* only column and loose variables may be member of a clique */
   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN || SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE )
   {
      /* delete clique from variable's clique list */
      SCIP_CALL( SCIPcliquelistDel(&var->cliquelist, blkmem, value, clique) );

      /* delete variable from clique */
      SCIP_CALL( SCIPcliqueDelVar(clique, var, value) );

      /* check consistency of cliquelist */
      SCIPcliquelistCheck(var->cliquelist, var);
   }

   return SCIP_OKAY;
}

/** returns whether there is a clique that contains both given variable/value pairs;
 *  the variables must be active binary variables;
 *  if regardimplics is FALSE, only the cliques in the clique table are looked at;
 *  if regardimplics is TRUE, both the cliques and the implications of the implication graph are regarded
 */
SCIP_Bool SCIPvarsHaveCommonClique(
   SCIP_VAR*             var1,               /**< first variable */
   SCIP_Bool             value1,             /**< value of first variable */
   SCIP_VAR*             var2,               /**< second variable */
   SCIP_Bool             value2,             /**< value of second variable */
   SCIP_Bool             regardimplics       /**< should the implication graph also be searched for a clique? */
   )
{
   assert(var1 != NULL);
   assert(var2 != NULL);
   assert(SCIPvarIsActive(var1));
   assert(SCIPvarIsActive(var2));
   assert(SCIPvarGetType(var1) == SCIP_VARTYPE_BINARY);
   assert(SCIPvarGetType(var2) == SCIP_VARTYPE_BINARY);

   return (SCIPcliquelistsHaveCommonClique(var1->cliquelist, value1, var2->cliquelist, value2)
      || (regardimplics && SCIPvarHasImplic(var1, value1, var2, value2 ? SCIP_BOUNDTYPE_UPPER : SCIP_BOUNDTYPE_LOWER)));
}

/** actually changes the branch factor of the variable and of all parent variables */
static
void varProcessChgBranchFactor(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             branchfactor        /**< factor to weigh variable's branching score with */
   )
{
   SCIP_VAR* parentvar;
   SCIP_Real eps;
   int i;

   assert(var != NULL);
   assert(set != NULL);

   /* only use positive values */
   eps = SCIPsetEpsilon(set);
   branchfactor = MAX(branchfactor, eps);

   SCIPdebugMessage("process changing branch factor of <%s> from %f to %f\n", var->name, var->branchfactor, branchfactor);

   if( SCIPsetIsEQ(set, branchfactor, var->branchfactor) )
      return;

   /* change the branch factor */
   var->branchfactor = branchfactor;

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         /* do not change priorities across the border between transformed and original problem */
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         SCIPABORT();
         break;

      case SCIP_VARSTATUS_AGGREGATED:
      case SCIP_VARSTATUS_NEGATED:
         varProcessChgBranchFactor(parentvar, set, branchfactor);
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         SCIPABORT();
      }
   }
}

/** sets the branch factor of the variable; this value can be used in the branching methods to scale the score
 *  values of the variables; higher factor leads to a higher probability that this variable is chosen for branching
 */
void SCIPvarChgBranchFactor(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real             branchfactor        /**< factor to weigh variable's branching score with */
   )
{
   int v;

   assert(var != NULL);
   assert(set != NULL);
   assert(branchfactor >= 0.0);

   SCIPdebugMessage("changing branch factor of <%s> from %g to %g\n", var->name, var->branchfactor, branchfactor);

   if( SCIPsetIsEQ(set, var->branchfactor, branchfactor) )
      return;

   /* change priorities of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
         SCIPvarChgBranchFactor(var->data.original.transvar, set, branchfactor);
      else
      {
         assert(set->stage == SCIP_STAGE_PROBLEM);
         var->branchfactor = branchfactor;
      }
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      varProcessChgBranchFactor(var, set, branchfactor);
      break;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      SCIPvarChgBranchFactor(var->data.aggregate.var, set, branchfactor);
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      for( v = 0; v < var->data.multaggr.nvars; ++v )
         SCIPvarChgBranchFactor(var->data.multaggr.vars[v], set, branchfactor);
      break;

   case SCIP_VARSTATUS_NEGATED:
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIPvarChgBranchFactor(var->negatedvar, set, branchfactor);
      break;
         
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
   }
}

/** actually changes the branch priority of the variable and of all parent variables */
static
void varProcessChgBranchPriority(
   SCIP_VAR*             var,                /**< problem variable */
   int                   branchpriority      /**< branching priority of the variable */
   )
{
   SCIP_VAR* parentvar;
   int i;

   assert(var != NULL);

   SCIPdebugMessage("process changing branch priority of <%s> from %d to %d\n", 
      var->name, var->branchpriority, branchpriority);

   if( branchpriority == var->branchpriority )
      return;

   /* change the branch priority */
   var->branchpriority = branchpriority;

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         /* do not change priorities across the border between transformed and original problem */
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         SCIPABORT();
         break;

      case SCIP_VARSTATUS_AGGREGATED:
      case SCIP_VARSTATUS_NEGATED:
         varProcessChgBranchPriority(parentvar, branchpriority);
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         SCIPABORT();
      }
   }
}

/** sets the branch priority of the variable; variables with higher branch priority are always preferred to variables
 *  with lower priority in selection of branching variable
 */
void SCIPvarChgBranchPriority(
   SCIP_VAR*             var,                /**< problem variable */
   int                   branchpriority      /**< branching priority of the variable */
   )
{
   int v;

   assert(var != NULL);

   SCIPdebugMessage("changing branch priority of <%s> from %d to %d\n", var->name, var->branchpriority, branchpriority);

   if( var->branchpriority == branchpriority )
      return;

   /* change priorities of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
         SCIPvarChgBranchPriority(var->data.original.transvar, branchpriority);
      else
         var->branchpriority = branchpriority;
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      varProcessChgBranchPriority(var, branchpriority);
      break;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      SCIPvarChgBranchPriority(var->data.aggregate.var, branchpriority);
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      for( v = 0; v < var->data.multaggr.nvars; ++v )
         SCIPvarChgBranchPriority(var->data.multaggr.vars[v], branchpriority);
      break;

   case SCIP_VARSTATUS_NEGATED:
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIPvarChgBranchPriority(var->negatedvar, branchpriority);
      break;
         
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
   }
}

/** actually changes the branch direction of the variable and of all parent variables */
static
void varProcessChgBranchDirection(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        branchdirection     /**< preferred branch direction of the variable (downwards, upwards, auto) */
   )
{
   SCIP_VAR* parentvar;
   int i;

   assert(var != NULL);

   SCIPdebugMessage("process changing branch direction of <%s> from %d to %d\n", 
      var->name, var->branchdirection, branchdirection);

   if( branchdirection == (SCIP_BRANCHDIR)var->branchdirection )
      return;

   /* change the branch direction */
   var->branchdirection = branchdirection; /*lint !e641*/

   /* process parent variables */
   for( i = 0; i < var->nparentvars; ++i )
   {
      parentvar = var->parentvars[i];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         /* do not change directions across the border between transformed and original problem */
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         SCIPABORT();
         break;

      case SCIP_VARSTATUS_AGGREGATED:
         if( parentvar->data.aggregate.scalar > 0.0 )
            varProcessChgBranchDirection(parentvar, branchdirection);
         else
            varProcessChgBranchDirection(parentvar, SCIPbranchdirOpposite(branchdirection));
         break;

      case SCIP_VARSTATUS_NEGATED:
         varProcessChgBranchDirection(parentvar, SCIPbranchdirOpposite(branchdirection));
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         SCIPABORT();
      }
   }
}

/** sets the branch direction of the variable; variables with higher branch direction are always preferred to variables
 *  with lower direction in selection of branching variable
 */
void SCIPvarChgBranchDirection(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        branchdirection     /**< preferred branch direction of the variable (downwards, upwards, auto) */
   )
{
   int v;

   assert(var != NULL);

   SCIPdebugMessage("changing branch direction of <%s> from %d to %d\n", var->name, var->branchdirection, branchdirection);

   if( (SCIP_BRANCHDIR)var->branchdirection == branchdirection )
      return;

   /* change directions of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar != NULL )
         SCIPvarChgBranchDirection(var->data.original.transvar, branchdirection);
      else
         var->branchdirection = branchdirection; /*lint !e641*/
      break;
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      varProcessChgBranchDirection(var, branchdirection);
      break;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      if( var->data.aggregate.scalar > 0.0 )
         SCIPvarChgBranchDirection(var->data.aggregate.var, branchdirection);
      else
         SCIPvarChgBranchDirection(var->data.aggregate.var, SCIPbranchdirOpposite(branchdirection));
      break;
         
   case SCIP_VARSTATUS_MULTAGGR:
      for( v = 0; v < var->data.multaggr.nvars; ++v )
      {
         /* only update branching direction of aggregation variables, if they don't have a preferred direction yet */
         assert(var->data.multaggr.vars[v] != NULL);
         if( (SCIP_BRANCHDIR)var->data.multaggr.vars[v]->branchdirection == SCIP_BRANCHDIR_AUTO )
         {
            if( var->data.multaggr.scalars[v] > 0.0 )
               SCIPvarChgBranchDirection(var->data.multaggr.vars[v], branchdirection);
            else
               SCIPvarChgBranchDirection(var->data.multaggr.vars[v], SCIPbranchdirOpposite(branchdirection));
         }
      }
      break;

   case SCIP_VARSTATUS_NEGATED:
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIPvarChgBranchDirection(var->negatedvar, SCIPbranchdirOpposite(branchdirection));
      break;
         
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
   }
}

/** compares the index of two variables, returns -1 if first is smaller than, and +1 if first is greater than second
 *  variable index; returns 0 if both indices are equal, which means both variables are equal
 */
int SCIPvarCompare(
   SCIP_VAR*             var1,               /**< first problem variable */
   SCIP_VAR*             var2                /**< second problem variable */
   )
{
   assert(var1 != NULL);
   assert(var2 != NULL);

   if( var1->index < var2->index )
      return -1;
   else if( var1->index > var2->index )
      return +1;
   else
   {
      assert(var1 == var2);
      return 0;
   }
}

/** comparison method for sorting variables by non-decreasing index */
SCIP_DECL_SORTPTRCOMP(SCIPvarComp)
{
   return SCIPvarCompare((SCIP_VAR*)elem1, (SCIP_VAR*)elem2);
}

/** gets corresponding active, fixed, or multi-aggregated problem variable of a variable */
SCIP_VAR* SCIPvarGetProbvar(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   SCIPdebugMessage("get problem variable of <%s>\n", var->name);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("original variable has no transformed variable attached\n");
         return NULL;
      }
      return SCIPvarGetProbvar(var->data.original.transvar);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_FIXED:
   case SCIP_VARSTATUS_MULTAGGR:
      return var;

   case SCIP_VARSTATUS_AGGREGATED:
      return SCIPvarGetProbvar(var->data.aggregate.var);

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetProbvar(var->negatedvar);

   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return NULL;
   }
}

/** gets corresponding active, fixed, or multi-aggregated problem variable of a binary variable and updates the given
 *  negation status
 */
SCIP_RETCODE SCIPvarGetProbvarBinary(
   SCIP_VAR**            var,                /**< pointer to binary problem variable */
   SCIP_Bool*            negated             /**< pointer to update the negation status */
   )
{
   assert(var != NULL);
   assert(*var != NULL);
   assert(negated != NULL);

   while( *var != NULL )
   {
      assert(SCIPvarGetType(*var) == SCIP_VARTYPE_BINARY);

      switch( SCIPvarGetStatus(*var) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         if( (*var)->data.original.transvar == NULL )
            return SCIP_OKAY;
         *var = (*var)->data.original.transvar;
         break;

      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         return SCIP_OKAY;

      case SCIP_VARSTATUS_AGGREGATED:  /* x = a'*x' + c'  =>  a*x + c == (a*a')*x' + (a*c' + c) */
         assert((*var)->data.aggregate.var != NULL);
         assert(SCIPvarGetType((*var)->data.aggregate.var) == SCIP_VARTYPE_BINARY);
         assert(EPSEQ((*var)->data.aggregate.constant, 0.0, 1e-06) || EPSEQ((*var)->data.aggregate.constant, 1.0, 1e-06));
         assert(EPSEQ((*var)->data.aggregate.scalar, 1.0, 1e-06) || EPSEQ((*var)->data.aggregate.scalar, -1.0, 1e-06));
         assert(EPSEQ((*var)->data.aggregate.constant, 0.0, 1e-06) == EPSEQ((*var)->data.aggregate.scalar, 1.0, 1e-06));
         *negated = (*negated != ((*var)->data.aggregate.scalar < 0.0));
         *var = (*var)->data.aggregate.var;
         break;

      case SCIP_VARSTATUS_NEGATED:     /* x =  - x' + c'  =>  a*x + c ==   (-a)*x' + (a*c' + c) */
         assert((*var)->negatedvar != NULL);
         *negated = !(*negated);
         *var = (*var)->negatedvar;
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }

   SCIPerrorMessage("active variable path leads to NULL pointer\n");
   return SCIP_INVALIDDATA;
}

/** transforms given variable, boundtype and bound to the corresponding active, fixed, or multi-aggregated variable
 *  values
 */
SCIP_RETCODE SCIPvarGetProbvarBound(
   SCIP_VAR**            var,                /**< pointer to problem variable */
   SCIP_Real*            bound,              /**< pointer to bound value to transform */
   SCIP_BOUNDTYPE*       boundtype           /**< pointer to type of bound: lower or upper bound */
   )
{
   assert(var != NULL);
   assert(*var != NULL);
   assert(bound != NULL);
   assert(boundtype != NULL);

   SCIPdebugMessage("get probvar bound %g of type %d of variable <%s>\n", *bound, *boundtype, (*var)->name);

   switch( SCIPvarGetStatus(*var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( (*var)->data.original.transvar == NULL )
      {
         SCIPerrorMessage("original variable has no transformed variable attached\n");
         return SCIP_INVALIDDATA;
      }
      *var = (*var)->data.original.transvar;
      SCIP_CALL( SCIPvarGetProbvarBound(var, bound, boundtype) );
      break;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_FIXED:
   case SCIP_VARSTATUS_MULTAGGR:
      break;
      
   case SCIP_VARSTATUS_AGGREGATED:  /* x = a*y + c  ->  y = x/a - c/a */
      assert((*var)->data.aggregate.var != NULL);
      assert((*var)->data.aggregate.scalar != 0.0);
      
      (*bound) /= (*var)->data.aggregate.scalar;
      (*bound) -= (*var)->data.aggregate.constant/(*var)->data.aggregate.scalar;
      if( (*var)->data.aggregate.scalar < 0.0 )
      {
         if( *boundtype == SCIP_BOUNDTYPE_LOWER )
            *boundtype = SCIP_BOUNDTYPE_UPPER;
         else
            *boundtype = SCIP_BOUNDTYPE_LOWER;
      }
      *var = (*var)->data.aggregate.var;
      SCIP_CALL( SCIPvarGetProbvarBound(var, bound, boundtype) );
      break;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert((*var)->negatedvar != NULL);
      assert(SCIPvarGetStatus((*var)->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert((*var)->negatedvar->negatedvar == *var);
      (*bound) = (*var)->data.negate.constant - *bound;
      if( *boundtype == SCIP_BOUNDTYPE_LOWER )
         *boundtype = SCIP_BOUNDTYPE_UPPER;
      else
         *boundtype = SCIP_BOUNDTYPE_LOWER;
      *var = (*var)->negatedvar;
      SCIP_CALL( SCIPvarGetProbvarBound(var, bound, boundtype) );
      break;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** transforms given variable, scalar and constant to the corresponding active, fixed, or multi-aggregated variable,
 *  scalar and constant;
 *  if the variable resolves to a fixed variable, "scalar" will be 0.0 and the value of the sum will be stored
 *  in "constant"
 */
SCIP_RETCODE SCIPvarGetProbvarSum(
   SCIP_VAR**            var,                /**< pointer to problem variable x in sum a*x + c */
   SCIP_Real*            scalar,             /**< pointer to scalar a in sum a*x + c */
   SCIP_Real*            constant            /**< pointer to constant c in sum a*x + c */
   )
{
   assert(var != NULL);
   assert(scalar != NULL);
   assert(constant != NULL);

   while( *var != NULL )
   {
      switch( SCIPvarGetStatus(*var) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         if( (*var)->data.original.transvar == NULL )
         {
            SCIPerrorMessage("original variable has no transformed variable attached\n");
            return SCIP_INVALIDDATA;
         }
         *var = (*var)->data.original.transvar;
         break;

      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_MULTAGGR:
         return SCIP_OKAY;

      case SCIP_VARSTATUS_FIXED:       /* x = c'          =>  a*x + c ==             (a*c' + c) */
         (*constant) += *scalar * (*var)->glbdom.lb;
         *scalar = 0.0;
         return SCIP_OKAY;

      case SCIP_VARSTATUS_AGGREGATED:  /* x = a'*x' + c'  =>  a*x + c == (a*a')*x' + (a*c' + c) */
         assert((*var)->data.aggregate.var != NULL);
         (*constant) += *scalar * (*var)->data.aggregate.constant;
         (*scalar) *= (*var)->data.aggregate.scalar;
         *var = (*var)->data.aggregate.var;
         break;

      case SCIP_VARSTATUS_NEGATED:     /* x =  - x' + c'  =>  a*x + c ==   (-a)*x' + (a*c' + c) */
         assert((*var)->negatedvar != NULL);
         assert(SCIPvarGetStatus((*var)->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert((*var)->negatedvar->negatedvar == *var);
         (*constant) += *scalar * (*var)->data.negate.constant;
         (*scalar) *= -1.0;
         *var = (*var)->negatedvar;
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }
   }
   *scalar = 0.0;

   return SCIP_OKAY;
}

/** retransforms given variable, scalar and constant to the corresponding original variable, scalar and constant,
 *  if possible;
 *  if the retransformation is impossible, NULL is returned as variable
 */
SCIP_RETCODE SCIPvarGetOrigvarSum(
   SCIP_VAR**            var,                /**< pointer to problem variable x in sum a*x + c */
   SCIP_Real*            scalar,             /**< pointer to scalar a in sum a*x + c */
   SCIP_Real*            constant            /**< pointer to constant c in sum a*x + c */
   )
{
   SCIP_VAR* parentvar;

   assert(var != NULL);
   assert(*var != NULL);
   assert(scalar != NULL);
   assert(constant != NULL);

   while( SCIPvarGetStatus(*var) != SCIP_VARSTATUS_ORIGINAL )
   {
      /* if the variable has no parent variables, it was generated during solving and has no corresponding original var */
      if( (*var)->nparentvars == 0 )
      {
         *var = NULL;
         return SCIP_OKAY;
      }

      /* follow the link to the first parent variable */
      parentvar = (*var)->parentvars[0];
      assert(parentvar != NULL);

      switch( SCIPvarGetStatus(parentvar) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
         break;
         
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_FIXED:
      case SCIP_VARSTATUS_MULTAGGR:
         SCIPerrorMessage("column, loose, fixed or multi-aggregated variable cannot be the parent of a variable\n");
         return SCIP_INVALIDDATA;
      
      case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + b  ->  y = (x-b)/a,  s*y + c = (s/a)*x + c-b*s/a */
         assert(parentvar->data.aggregate.var == *var);
         assert(parentvar->data.aggregate.scalar != 0.0);
         *scalar /= parentvar->data.aggregate.scalar;
         *constant -= parentvar->data.aggregate.constant * (*scalar);
         break;

      case SCIP_VARSTATUS_NEGATED: /* x = b - y  ->  y = b - x,  s*y + c = -s*x + c+b*s */
         assert(parentvar->negatedvar != NULL);
         assert(SCIPvarGetStatus(parentvar->negatedvar) != SCIP_VARSTATUS_NEGATED);
         assert(parentvar->negatedvar->negatedvar == parentvar);
         *scalar *= -1.0;
         *constant -= parentvar->data.negate.constant * (*scalar);
         break;

      default:
         SCIPerrorMessage("unknown variable status\n");
         return SCIP_INVALIDDATA;
      }

      *var = parentvar;
   }

   return SCIP_OKAY;
}

/** returns whether the given variable is the direct counterpart of an original problem variable */
SCIP_Bool SCIPvarIsTransformedOrigvar(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   /* the corresponding original variable is always the first parent variable of the transformed variable */
   return (SCIPvarIsTransformed(var)
      && var->nparentvars >= 1 && SCIPvarGetStatus(var->parentvars[0]) == SCIP_VARSTATUS_ORIGINAL);
}

/** gets objective value of variable in current SCIP_LP; the value can be different from the bound stored in the variable's own
 *  data due to diving, that operate only on the LP without updating the variables
 */
SCIP_Real SCIPvarGetObjLP(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   /* get bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      return SCIPvarGetObjLP(var->data.original.transvar);
         
   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      return SCIPcolGetObj(var->data.col);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      return var->obj;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      return var->data.aggregate.scalar * SCIPvarGetObjLP(var->data.aggregate.var);
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot get the objective value of a multiple aggregated variable\n");
      SCIPABORT();
      return 0.0;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return -SCIPvarGetObjLP(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets lower bound of variable in current SCIP_LP; the bound can be different from the bound stored in the variable's own
 *  data due to diving or conflict analysis, that operate only on the LP without updating the variables
 */
SCIP_Real SCIPvarGetLbLP(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   /* get bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      return SCIPvarGetLbLP(var->data.original.transvar);
         
   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      return SCIPcolGetLb(var->data.col);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      return var->locdom.lb;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( var->data.aggregate.scalar > 0.0 )
      {
         /* a > 0 -> get lower bound of y */
         return var->data.aggregate.scalar * SCIPvarGetLbLP(var->data.aggregate.var) + var->data.aggregate.constant;
      }
      else if( var->data.aggregate.scalar < 0.0 )
      {
         /* a < 0 -> get upper bound of y */
         return var->data.aggregate.scalar * SCIPvarGetUbLP(var->data.aggregate.var) + var->data.aggregate.constant;
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         SCIPABORT();
         return 0.0;
      }
      
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo get the sides of the corresponding linear constraint */
      SCIPerrorMessage("getting the bounds of a multiple aggregated variable is not implemented yet\n");
      SCIPABORT();
      return 0.0;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetUbLP(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets upper bound of variable in current SCIP_LP; the bound can be different from the bound stored in the variable's own
 *  data due to diving or conflict analysis, that operate only on the LP without updating the variables
 */
SCIP_Real SCIPvarGetUbLP(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   /* get bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      return SCIPvarGetUbLP(var->data.original.transvar);
         
   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      return SCIPcolGetUb(var->data.col);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_FIXED:
      return var->locdom.ub;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( var->data.aggregate.scalar > 0.0 )
      {
         /* a > 0 -> get upper bound of y */
         return var->data.aggregate.scalar * SCIPvarGetUbLP(var->data.aggregate.var) + var->data.aggregate.constant;
      }
      else if( var->data.aggregate.scalar < 0.0 )
      {
         /* a < 0 -> get lower bound of y */
         return var->data.aggregate.scalar * SCIPvarGetLbLP(var->data.aggregate.var) + var->data.aggregate.constant;
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         SCIPABORT();
         return 0.0;
      }
      
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo get the sides of the corresponding linear constraint */
      SCIPerrorMessage("getting the bounds of a multiple aggregated variable is not implemented yet\n");
      SCIPABORT();
      return 0.0;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetLbLP(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets best local bound of variable with respect to the objective function */
SCIP_Real SCIPvarGetBestBound(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   if( var->obj >= 0.0 )
      return var->locdom.lb;
   else
      return var->locdom.ub;
}

/** gets worst local bound of variable with respect to the objective function */
SCIP_Real SCIPvarGetWorstBound(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   if( var->obj >= 0.0 )
      return var->locdom.ub;
   else
      return var->locdom.lb;
}

/** gets type (lower or upper) of best bound of variable with respect to the objective function */
SCIP_BOUNDTYPE SCIPvarGetBestBoundType(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   if( var->obj >= 0.0 )
      return SCIP_BOUNDTYPE_LOWER;
   else
      return SCIP_BOUNDTYPE_UPPER;
}

/** gets type (lower or upper) of worst bound of variable with respect to the objective function */
SCIP_BOUNDTYPE SCIPvarGetWorstBoundType(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   if( var->obj >= 0.0 )
      return SCIP_BOUNDTYPE_UPPER;
   else
      return SCIP_BOUNDTYPE_LOWER;
}

/** gets primal LP solution value of variable */
SCIP_Real SCIPvarGetLPSol(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   SCIP_Real primsol;
   int i;

   assert(var != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIP_INVALID;
      return SCIPvarGetLPSol(var->data.original.transvar);

   case SCIP_VARSTATUS_LOOSE:
      return SCIPvarGetBestBound(var);

   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      return SCIPcolGetPrimsol(var->data.col);

   case SCIP_VARSTATUS_FIXED:
      assert(var->locdom.lb == var->locdom.ub); /*lint !e777*/
      return var->locdom.lb;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      return var->data.aggregate.scalar * SCIPvarGetLPSol(var->data.aggregate.var) + var->data.aggregate.constant;

   case SCIP_VARSTATUS_MULTAGGR:
      assert(var->data.multaggr.vars != NULL);
      assert(var->data.multaggr.scalars != NULL);
      assert(var->data.multaggr.nvars >= 2);
      primsol = var->data.multaggr.constant;
      for( i = 0; i < var->data.multaggr.nvars; ++i )
         primsol += var->data.multaggr.scalars[i] * SCIPvarGetLPSol(var->data.multaggr.vars[i]);
      return primsol;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetLPSol(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets pseudo solution value of variable at current node */
SCIP_Real SCIPvarGetPseudoSol(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   SCIP_Real pseudosol;
   int i;

   assert(var != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIP_INVALID;
      return SCIPvarGetPseudoSol(var->data.original.transvar);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPvarGetBestBound(var);

   case SCIP_VARSTATUS_FIXED:
      assert(var->locdom.lb == var->locdom.ub); /*lint !e777*/
      return var->locdom.lb;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      return var->data.aggregate.scalar * SCIPvarGetPseudoSol(var->data.aggregate.var) + var->data.aggregate.constant;

   case SCIP_VARSTATUS_MULTAGGR:
      assert(var->data.multaggr.vars != NULL);
      assert(var->data.multaggr.scalars != NULL);
      assert(var->data.multaggr.nvars >= 2);
      pseudosol = var->data.multaggr.constant;
      for( i = 0; i < var->data.multaggr.nvars; ++i )
         pseudosol += var->data.multaggr.scalars[i] * SCIPvarGetPseudoSol(var->data.multaggr.vars[i]);
      return pseudosol;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetPseudoSol(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets current LP or pseudo solution value of variable */
SCIP_Real SCIPvarGetSol(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_Bool             getlpval            /**< should the LP solution value be returned? */
   )
{
   if( getlpval )
      return SCIPvarGetLPSol(var);
   else
      return SCIPvarGetPseudoSol(var);
}

/** remembers the current solution as root solution in the problem variables */
void SCIPvarStoreRootSol(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_Bool             roothaslp           /**< is the root solution from LP? */
   )
{
   assert(var != NULL);

   var->rootsol = SCIPvarGetSol(var, roothaslp);
}

/** returns the solution of the variable in the root node's relaxation, if the root relaxation is not yet completely
 *  solved, zero is returned
 */
SCIP_Real SCIPvarGetRootSol(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   SCIP_Real rootsol;
   int i;

   assert(var != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      return SCIPvarGetRootSol(var->data.original.transvar);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return var->rootsol;

   case SCIP_VARSTATUS_FIXED:
      assert(var->locdom.lb == var->locdom.ub); /*lint !e777*/
      return var->locdom.lb;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      return var->data.aggregate.scalar * SCIPvarGetRootSol(var->data.aggregate.var) + var->data.aggregate.constant;

   case SCIP_VARSTATUS_MULTAGGR:
      assert(var->data.multaggr.vars != NULL);
      assert(var->data.multaggr.scalars != NULL);
      assert(var->data.multaggr.nvars >= 2);
      rootsol = var->data.multaggr.constant;
      for( i = 0; i < var->data.multaggr.nvars; ++i )
         rootsol += var->data.multaggr.scalars[i] * SCIPvarGetRootSol(var->data.multaggr.vars[i]);
      return rootsol;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetRootSol(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns a weighted average solution value of the variable in all feasible primal solutions found so far */
SCIP_Real SCIPvarGetAvgSol(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   SCIP_Real avgsol;
   int i;

   assert(var != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      return SCIPvarGetAvgSol(var->data.original.transvar);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      avgsol = var->primsolavg;
      avgsol = MIN(avgsol, var->glbdom.lb);
      avgsol = MAX(avgsol, var->glbdom.ub);
      return avgsol;

   case SCIP_VARSTATUS_FIXED:
      assert(var->locdom.lb == var->locdom.ub); /*lint !e777*/
      return var->locdom.lb;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      return var->data.aggregate.scalar * SCIPvarGetAvgSol(var->data.aggregate.var)
         + var->data.aggregate.constant;

   case SCIP_VARSTATUS_MULTAGGR:
      assert(var->data.multaggr.vars != NULL);
      assert(var->data.multaggr.scalars != NULL);
      assert(var->data.multaggr.nvars >= 2);
      avgsol = var->data.multaggr.constant;
      for( i = 0; i < var->data.multaggr.nvars; ++i )
         avgsol += var->data.multaggr.scalars[i] * SCIPvarGetAvgSol(var->data.multaggr.vars[i]);
      return avgsol;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetAvgSol(var->negatedvar);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** resolves variable to columns and adds them with the coefficient to the row */
SCIP_RETCODE SCIPvarAddToRow(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_ROW*             row,                /**< LP row */
   SCIP_Real             val                 /**< value of coefficient */
   )
{
   int i;

   assert(var != NULL);
   assert(row != NULL);
   assert(!SCIPsetIsInfinity(set, REALABS(val)));

   SCIPdebugMessage("adding coefficient %g<%s> to row <%s>\n", val, var->name, row->name);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot add untransformed original variable <%s> to LP row <%s>\n", var->name, row->name);
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarAddToRow(var->data.original.transvar, blkmem, set, stat, prob, lp, row, val) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
      /* convert loose variable into column */
      SCIP_CALL( SCIPvarColumn(var, blkmem, set, stat, prob, lp) );
      assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);
      /*lint -fallthrough*/

   case SCIP_VARSTATUS_COLUMN:
      assert(var->data.col != NULL);
      assert(var->data.col->var == var);
      SCIP_CALL( SCIProwIncCoef(row, blkmem, set, lp, var->data.col, val) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      assert(var->glbdom.lb == var->glbdom.ub); /*lint !e777*/
      assert(var->locdom.lb == var->locdom.ub); /*lint !e777*/
      assert(var->locdom.lb == var->glbdom.lb); /*lint !e777*/
      assert(!SCIPsetIsInfinity(set, REALABS(var->locdom.lb)));
      SCIP_CALL( SCIProwAddConstant(row, set, stat, lp, val * var->locdom.lb) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(var->data.aggregate.var != NULL);
      SCIP_CALL( SCIPvarAddToRow(var->data.aggregate.var, blkmem, set, stat, prob, lp,
            row, var->data.aggregate.scalar * val) );
      SCIP_CALL( SCIProwAddConstant(row, set, stat, lp, var->data.aggregate.constant * val) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_MULTAGGR:
      assert(var->data.multaggr.vars != NULL);
      assert(var->data.multaggr.scalars != NULL);
      assert(var->data.multaggr.nvars >= 2);
      for( i = 0; i < var->data.multaggr.nvars; ++i )
      {
         SCIP_CALL( SCIPvarAddToRow(var->data.multaggr.vars[i], blkmem, set, stat, prob, lp,
               row, var->data.multaggr.scalars[i] * val) );
      }
      SCIP_CALL( SCIProwAddConstant(row, set, stat, lp, var->data.multaggr.constant * val) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      SCIP_CALL( SCIPvarAddToRow(var->negatedvar, blkmem, set, stat, prob, lp, row, -val) );
      SCIP_CALL( SCIProwAddConstant(row, set, stat, lp, var->data.negate.constant * val) );
      return SCIP_OKAY;

   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** updates the pseudo costs of the given variable and the global pseudo costs after a change of
 *  "solvaldelta" in the variable's solution value and resulting change of "objdelta" in the in the LP's objective value
 */
SCIP_RETCODE SCIPvarUpdatePseudocost(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_Real             solvaldelta,        /**< difference of variable's new LP value - old LP value */
   SCIP_Real             objdelta,           /**< difference of new LP's objective value - old LP's objective value */
   SCIP_Real             weight              /**< weight in (0,1] of this update in pseudo cost sum */
   )
{
   assert(var != NULL);
   assert(stat != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot update pseudo costs of original untransformed variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarUpdatePseudocost(var->data.original.transvar, set, stat, solvaldelta, objdelta, weight) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      SCIPhistoryUpdatePseudocost(var->history, set, solvaldelta, objdelta, weight);
      SCIPhistoryUpdatePseudocost(var->historycrun, set, solvaldelta, objdelta, weight);
      SCIPhistoryUpdatePseudocost(stat->glbhistory, set, solvaldelta, objdelta, weight);
      SCIPhistoryUpdatePseudocost(stat->glbhistorycrun, set, solvaldelta, objdelta, weight);
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot update pseudo cost values of a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      assert(!SCIPsetIsZero(set, var->data.aggregate.scalar));
      SCIP_CALL( SCIPvarUpdatePseudocost(var->data.aggregate.var, set, stat,
                     solvaldelta/var->data.aggregate.scalar, objdelta, weight) );
      return SCIP_OKAY;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot update pseudo cost values of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      SCIP_CALL( SCIPvarUpdatePseudocost(var->negatedvar, set, stat, -solvaldelta, objdelta, weight) );
      return SCIP_OKAY;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** gets the variable's pseudo cost value for the given step size "solvaldelta" in the variable's LP solution value */
SCIP_Real SCIPvarGetPseudocost(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_Real             solvaldelta         /**< difference of variable's new LP value - old LP value */
   )
{
   SCIP_BRANCHDIR dir;

   assert(var != NULL);
   assert(stat != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIPhistoryGetPseudocost(stat->glbhistory, solvaldelta);
      else
         return SCIPvarGetPseudocost(var->data.original.transvar, stat, solvaldelta);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      dir = (solvaldelta >= 0.0 ? SCIP_BRANCHDIR_UPWARDS : SCIP_BRANCHDIR_DOWNWARDS);
      
      return SCIPhistoryGetPseudocostCount(var->history, dir) > 0.0
         ? SCIPhistoryGetPseudocost(var->history, solvaldelta)
         : SCIPhistoryGetPseudocost(stat->glbhistory, solvaldelta);

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      return SCIPvarGetPseudocost(var->data.aggregate.var, stat, var->data.aggregate.scalar * solvaldelta);
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetPseudocost(var->negatedvar, stat, -solvaldelta);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets the variable's pseudo cost value for the given step size "solvaldelta" in the variable's LP solution value,
 *  only using the pseudo cost information of the current run
 */
SCIP_Real SCIPvarGetPseudocostCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_Real             solvaldelta         /**< difference of variable's new LP value - old LP value */
   )
{
   SCIP_BRANCHDIR dir;

   assert(var != NULL);
   assert(stat != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIPhistoryGetPseudocost(stat->glbhistorycrun, solvaldelta);
      else
         return SCIPvarGetPseudocostCurrentRun(var->data.original.transvar, stat, solvaldelta);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      dir = (solvaldelta >= 0.0 ? SCIP_BRANCHDIR_UPWARDS : SCIP_BRANCHDIR_DOWNWARDS);
      
      return SCIPhistoryGetPseudocostCount(var->historycrun, dir) > 0.0
         ? SCIPhistoryGetPseudocost(var->historycrun, solvaldelta)
         : SCIPhistoryGetPseudocost(stat->glbhistorycrun, solvaldelta);

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      return SCIPvarGetPseudocostCurrentRun(var->data.aggregate.var, stat, var->data.aggregate.scalar * solvaldelta);
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetPseudocostCurrentRun(var->negatedvar, stat, -solvaldelta);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets the variable's (possible fractional) number of pseudo cost updates for the given direction */
SCIP_Real SCIPvarGetPseudocostCount(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);
   
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      else
         return SCIPvarGetPseudocostCount(var->data.original.transvar, dir);
      
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetPseudocostCount(var->history, dir);
      
   case SCIP_VARSTATUS_FIXED:
      return 0.0;
      
   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetPseudocostCount(var->data.aggregate.var, dir);
      else
         return SCIPvarGetPseudocostCount(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetPseudocostCount(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** gets the variable's (possible fractional) number of pseudo cost updates for the given direction,
 *  only using the pseudo cost information of the current run
 */
SCIP_Real SCIPvarGetPseudocostCountCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);
   
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      else
         return SCIPvarGetPseudocostCountCurrentRun(var->data.original.transvar, dir);
      
   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetPseudocostCount(var->historycrun, dir);
      
   case SCIP_VARSTATUS_FIXED:
      return 0.0;
      
   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetPseudocostCountCurrentRun(var->data.aggregate.var, dir);
      else
         return SCIPvarGetPseudocostCountCurrentRun(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetPseudocostCountCurrentRun(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** increases the conflict score of the variable by the given weight */
SCIP_RETCODE SCIPvarIncConflictScore(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir,                /**< branching direction */
   SCIP_Real             weight              /**< weight of this update in conflict score */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot update conflict score of original untransformed variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarIncConflictScore(var->data.original.transvar, dir, weight) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      SCIPhistoryIncConflictScore(var->history, dir, weight);
      SCIPhistoryIncConflictScore(var->historycrun, dir, weight);
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot update conflict score of a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
      {
         SCIP_CALL( SCIPvarIncConflictScore(var->data.aggregate.var, dir, weight) );
      }
      else
      {
         assert(var->data.aggregate.scalar < 0.0);
         SCIP_CALL( SCIPvarIncConflictScore(var->data.aggregate.var, SCIPbranchdirOpposite(dir), weight) );
      }
      return SCIP_OKAY;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot update conflict score of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      SCIP_CALL( SCIPvarIncConflictScore(var->negatedvar, SCIPbranchdirOpposite(dir), weight) );
      return SCIP_OKAY;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** increases the conflict score of the variable by the given weight */
SCIP_RETCODE SCIPvarScaleConflictScores(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_Real             scalar              /**< scalar to multiply the conflict scores with */
   )
{
   assert(var != NULL);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot update conflict score of original untransformed variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarScaleConflictScores(var->data.original.transvar, scalar) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      SCIPhistoryScaleConflictScores(var->history, scalar);
      SCIPhistoryScaleConflictScores(var->historycrun, scalar);
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot update conflict score of a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      SCIP_CALL( SCIPvarScaleConflictScores(var->data.aggregate.var, scalar) );
      return SCIP_OKAY;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot update conflict score of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      SCIP_CALL( SCIPvarScaleConflictScores(var->negatedvar, scalar) );
      return SCIP_OKAY;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** increases the number of branchings counter of the variable */
SCIP_RETCODE SCIPvarIncNBranchings(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   int                   depth,              /**< depth at which the bound change took place */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot update branching counter of original untransformed variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarIncNBranchings(var->data.original.transvar, stat, depth, dir) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      SCIPhistoryIncNBranchings(var->history, depth, dir);
      SCIPhistoryIncNBranchings(var->historycrun, depth, dir);
      SCIPhistoryIncNBranchings(stat->glbhistory, depth, dir);
      SCIPhistoryIncNBranchings(stat->glbhistorycrun, depth, dir);
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot update branching counter of a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
      {
         SCIP_CALL( SCIPvarIncNBranchings(var->data.aggregate.var, stat, depth, dir) );
      }
      else
      {
         assert(var->data.aggregate.scalar < 0.0);
         SCIP_CALL( SCIPvarIncNBranchings(var->data.aggregate.var, stat, depth, SCIPbranchdirOpposite(dir)) );
      }
      return SCIP_OKAY;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot update branching counter of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      SCIP_CALL( SCIPvarIncNBranchings(var->negatedvar, stat, depth, SCIPbranchdirOpposite(dir)) );
      return SCIP_OKAY;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** increases the number of inferences counter of the variable */
SCIP_RETCODE SCIPvarIncNInferences(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot update inference counter of original untransformed variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarIncNInferences(var->data.original.transvar, stat, dir) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      SCIPhistoryIncNInferences(var->history, dir);
      SCIPhistoryIncNInferences(var->historycrun, dir);
      SCIPhistoryIncNInferences(stat->glbhistory, dir);
      SCIPhistoryIncNInferences(stat->glbhistorycrun, dir);
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot update inference counter of a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
      {
         SCIP_CALL( SCIPvarIncNInferences(var->data.aggregate.var, stat, dir) );
      }
      else
      {
         assert(var->data.aggregate.scalar < 0.0);
         SCIP_CALL( SCIPvarIncNInferences(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir)) );
      }
      return SCIP_OKAY;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot update inference counter of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      SCIP_CALL( SCIPvarIncNInferences(var->negatedvar, stat, SCIPbranchdirOpposite(dir)) );
      return SCIP_OKAY;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** increases the number of cutoffs counter of the variable */
SCIP_RETCODE SCIPvarIncNCutoffs(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
      {
         SCIPerrorMessage("cannot update cutoff counter of original untransformed variable\n");
         return SCIP_INVALIDDATA;
      }
      SCIP_CALL( SCIPvarIncNCutoffs(var->data.original.transvar, stat, dir) );
      return SCIP_OKAY;

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      SCIPhistoryIncNCutoffs(var->history, dir);
      SCIPhistoryIncNCutoffs(var->historycrun, dir);
      SCIPhistoryIncNCutoffs(stat->glbhistory, dir);
      SCIPhistoryIncNCutoffs(stat->glbhistorycrun, dir);
      return SCIP_OKAY;

   case SCIP_VARSTATUS_FIXED:
      SCIPerrorMessage("cannot update cutoff counter of a fixed variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
      {
         SCIP_CALL( SCIPvarIncNCutoffs(var->data.aggregate.var, stat, dir) );
      }
      else
      {
         assert(var->data.aggregate.scalar < 0.0);
         SCIP_CALL( SCIPvarIncNCutoffs(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir)) );
      }
      return SCIP_OKAY;
      
   case SCIP_VARSTATUS_MULTAGGR:
      SCIPerrorMessage("cannot update cutoff counter of a multi-aggregated variable\n");
      return SCIP_INVALIDDATA;

   case SCIP_VARSTATUS_NEGATED:
      SCIP_CALL( SCIPvarIncNCutoffs(var->negatedvar, stat, SCIPbranchdirOpposite(dir)) );
      return SCIP_OKAY;
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      return SCIP_INVALIDDATA;
   }
}

/** returns the number of times, a bound of the variable was changed in given direction due to branching */
SCIP_Longint SCIPvarGetNBranchings(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0;
      else
         return SCIPvarGetNBranchings(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNBranchings(var->history, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNBranchings(var->data.aggregate.var, dir);
      else
         return SCIPvarGetNBranchings(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetNBranchings(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** returns the number of times, a bound of the variable was changed in given direction due to branching 
 *  in the current run
 */
SCIP_Longint SCIPvarGetNBranchingsCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0;
      else
         return SCIPvarGetNBranchingsCurrentRun(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNBranchings(var->historycrun, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNBranchingsCurrentRun(var->data.aggregate.var, dir);
      else
         return SCIPvarGetNBranchingsCurrentRun(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetNBranchingsCurrentRun(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** returns the average depth of bound changes in given direction due to branching on the variable */
SCIP_Real SCIPvarGetAvgBranchdepth(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      else
         return SCIPvarGetAvgBranchdepth(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return  SCIPhistoryGetAvgBranchdepth(var->history, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetAvgBranchdepth(var->data.aggregate.var, dir);
      else
         return SCIPvarGetAvgBranchdepth(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetAvgBranchdepth(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the average depth of bound changes in given direction due to branching on the variable
 *  in the current run
 */
SCIP_Real SCIPvarGetAvgBranchdepthCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      else
         return SCIPvarGetAvgBranchdepthCurrentRun(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return  SCIPhistoryGetAvgBranchdepth(var->historycrun, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetAvgBranchdepthCurrentRun(var->data.aggregate.var, dir);
      else
         return SCIPvarGetAvgBranchdepthCurrentRun(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetAvgBranchdepthCurrentRun(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the average number of inferences found after branching on the variable in given direction */
SCIP_Real SCIPvarGetConflictScore(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      else
         return SCIPvarGetConflictScore(var->data.original.transvar, stat, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetConflictScore(var->history, dir)/stat->conflictscoreweight;

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetConflictScore(var->data.aggregate.var, stat, dir);
      else
         return SCIPvarGetConflictScore(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetConflictScore(var->negatedvar, stat, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the average number of inferences found after branching on the variable in given direction
 *  in the current run
 */
SCIP_Real SCIPvarGetConflictScoreCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0.0;
      else
         return SCIPvarGetConflictScoreCurrentRun(var->data.original.transvar, stat, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetConflictScore(var->historycrun, dir)/stat->conflictscoreweight;

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetConflictScoreCurrentRun(var->data.aggregate.var, stat, dir);
      else
         return SCIPvarGetConflictScoreCurrentRun(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetConflictScoreCurrentRun(var->negatedvar, stat, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the number of inferences branching on this variable in given direction triggered */
SCIP_Longint SCIPvarGetNInferences(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0;
      else
         return SCIPvarGetNInferences(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNInferences(var->history, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNInferences(var->data.aggregate.var, dir);
      else
         return SCIPvarGetNInferences(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetNInferences(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** returns the number of inferences branching on this variable in given direction triggered
 *  in the current run
 */
SCIP_Longint SCIPvarGetNInferencesCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0;
      else
         return SCIPvarGetNInferencesCurrentRun(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNInferences(var->historycrun, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNInferencesCurrentRun(var->data.aggregate.var, dir);
      else
         return SCIPvarGetNInferencesCurrentRun(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetNInferencesCurrentRun(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** returns the average number of inferences found after branching on the variable in given direction */
SCIP_Real SCIPvarGetAvgInferences(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIPhistoryGetAvgInferences(stat->glbhistory, dir);
      else
         return SCIPvarGetAvgInferences(var->data.original.transvar, stat, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNBranchings(var->history, dir) > 0
         ? SCIPhistoryGetAvgInferences(var->history, dir)
         : (var->implics != NULL && SCIPimplicsGetNImpls(var->implics, dir == SCIP_BRANCHDIR_UPWARDS) > 0
            ? SCIPimplicsGetNImpls(var->implics, dir == SCIP_BRANCHDIR_UPWARDS)
            : SCIPhistoryGetAvgInferences(stat->glbhistory, dir));

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetAvgInferences(var->data.aggregate.var, stat, dir);
      else
         return SCIPvarGetAvgInferences(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetAvgInferences(var->negatedvar, stat, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the average number of inferences found after branching on the variable in given direction
 *  in the current run
 */
SCIP_Real SCIPvarGetAvgInferencesCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIPhistoryGetAvgInferences(stat->glbhistorycrun, dir);
      else
         return SCIPvarGetAvgInferencesCurrentRun(var->data.original.transvar, stat, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNBranchings(var->historycrun, dir) > 0
         ? SCIPhistoryGetAvgInferences(var->historycrun, dir)
         : (var->implics != NULL && SCIPimplicsGetNImpls(var->implics, dir == SCIP_BRANCHDIR_UPWARDS) > 0
            ? SCIPimplicsGetNImpls(var->implics, dir == SCIP_BRANCHDIR_UPWARDS)
            : SCIPhistoryGetAvgInferences(stat->glbhistorycrun, dir));

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetAvgInferencesCurrentRun(var->data.aggregate.var, stat, dir);
      else
         return SCIPvarGetAvgInferencesCurrentRun(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetAvgInferencesCurrentRun(var->negatedvar, stat, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the number of cutoffs branching on this variable in given direction produced */
SCIP_Longint SCIPvarGetNCutoffs(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0;
      else
         return SCIPvarGetNCutoffs(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNCutoffs(var->history, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNCutoffs(var->data.aggregate.var, dir);
      else
         return SCIPvarGetNCutoffs(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetNCutoffs(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** returns the number of cutoffs branching on this variable in given direction produced in the current run */
SCIP_Longint SCIPvarGetNCutoffsCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return 0;
      else
         return SCIPvarGetNCutoffsCurrentRun(var->data.original.transvar, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNCutoffs(var->historycrun, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetNCutoffsCurrentRun(var->data.aggregate.var, dir);
      else
         return SCIPvarGetNCutoffsCurrentRun(var->data.aggregate.var, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetNCutoffsCurrentRun(var->negatedvar, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0;
   }
}

/** returns the average number of cutoffs found after branching on the variable in given direction */
SCIP_Real SCIPvarGetAvgCutoffs(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIPhistoryGetAvgCutoffs(stat->glbhistory, dir);
      else
         return SCIPvarGetAvgCutoffs(var->data.original.transvar, stat, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNBranchings(var->history, dir) > 0
         ? SCIPhistoryGetAvgCutoffs(var->history, dir)
         : SCIPhistoryGetAvgCutoffs(stat->glbhistory, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetAvgCutoffs(var->data.aggregate.var, stat, dir);
      else
         return SCIPvarGetAvgCutoffs(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetAvgCutoffs(var->negatedvar, stat, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns the average number of cutoffs found after branching on the variable in given direction in the current run */
SCIP_Real SCIPvarGetAvgCutoffsCurrentRun(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_BRANCHDIR        dir                 /**< branching direction (downwards, or upwards) */
   )
{
   assert(var != NULL);
   assert(stat != NULL);
   assert(dir == SCIP_BRANCHDIR_DOWNWARDS || dir == SCIP_BRANCHDIR_UPWARDS);

   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      if( var->data.original.transvar == NULL )
         return SCIPhistoryGetAvgCutoffs(stat->glbhistorycrun, dir);
      else
         return SCIPvarGetAvgCutoffsCurrentRun(var->data.original.transvar, stat, dir);

   case SCIP_VARSTATUS_LOOSE:
   case SCIP_VARSTATUS_COLUMN:
      return SCIPhistoryGetNBranchings(var->historycrun, dir) > 0
         ? SCIPhistoryGetAvgCutoffs(var->historycrun, dir)
         : SCIPhistoryGetAvgCutoffs(stat->glbhistorycrun, dir);

   case SCIP_VARSTATUS_FIXED:
      return 0.0;

   case SCIP_VARSTATUS_AGGREGATED:
      if( var->data.aggregate.scalar > 0.0 )
         return SCIPvarGetAvgCutoffsCurrentRun(var->data.aggregate.var, stat, dir);
      else
         return SCIPvarGetAvgCutoffsCurrentRun(var->data.aggregate.var, stat, SCIPbranchdirOpposite(dir));
      
   case SCIP_VARSTATUS_MULTAGGR:
      return 0.0;

   case SCIP_VARSTATUS_NEGATED:
      return SCIPvarGetAvgCutoffsCurrentRun(var->negatedvar, stat, SCIPbranchdirOpposite(dir));
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}




/*
 * information methods for bound changes
 */

/** returns the bound change information for the last lower bound change on given active problem variable before or
 *  after the bound change with the given index was applied;
 *  returns NULL, if no change to the lower bound was applied up to this point of time
 */
SCIP_BDCHGINFO* SCIPvarGetLbchgInfo(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   int i;

   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   /* search the correct bound change information for the given bound change index */
   if( after )
   {
      for( i = var->nlbchginfos-1; i >= 0; --i )
      {
         assert(var->lbchginfos[i].var == var);
         assert((SCIP_BOUNDTYPE)var->lbchginfos[i].boundtype == SCIP_BOUNDTYPE_LOWER);
         
         if( !SCIPbdchgidxIsEarlier(bdchgidx, &var->lbchginfos[i].bdchgidx) )
            return &var->lbchginfos[i];
      }
   }
   else
   {
      for( i = var->nlbchginfos-1; i >= 0; --i )
      {
         assert(var->lbchginfos[i].var == var);
         assert((SCIP_BOUNDTYPE)var->lbchginfos[i].boundtype == SCIP_BOUNDTYPE_LOWER);
         
         if( SCIPbdchgidxIsEarlier(&var->lbchginfos[i].bdchgidx, bdchgidx) )
            return &var->lbchginfos[i];
      }
   }

   return NULL;
}

/** returns the bound change information for the last upper bound change on given active problem variable before or
 *  after the bound change with the given index was applied;
 *  returns NULL, if no change to the upper bound was applied up to this point of time
 */
SCIP_BDCHGINFO* SCIPvarGetUbchgInfo(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   int i;

   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   /* search the correct bound change information for the given bound change index */
   if( after )
   {
      for( i = var->nubchginfos-1; i >= 0; --i )
      {
         assert(var->ubchginfos[i].var == var);
         assert((SCIP_BOUNDTYPE)var->ubchginfos[i].boundtype == SCIP_BOUNDTYPE_UPPER);
         
         if( !SCIPbdchgidxIsEarlier(bdchgidx, &var->ubchginfos[i].bdchgidx) )
            return &var->ubchginfos[i];
      }
   }
   else
   {
      for( i = var->nubchginfos-1; i >= 0; --i )
      {
         assert(var->ubchginfos[i].var == var);
         assert((SCIP_BOUNDTYPE)var->ubchginfos[i].boundtype == SCIP_BOUNDTYPE_UPPER);
         
         if( SCIPbdchgidxIsEarlier(&var->ubchginfos[i].bdchgidx, bdchgidx) )
            return &var->ubchginfos[i];
      }
   }

   return NULL;
}

/** returns the bound change information for the last lower or upper bound change on given active problem variable
 *  before or after the bound change with the given index was applied;
 *  returns NULL, if no change to the lower/upper bound was applied up to this point of time
 */
SCIP_BDCHGINFO* SCIPvarGetBdchgInfo(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_BOUNDTYPE        boundtype,          /**< type of bound: lower or upper bound */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   if( boundtype == SCIP_BOUNDTYPE_LOWER )
      return SCIPvarGetLbchgInfo(var, bdchgidx, after);
   else
   {
      assert(boundtype == SCIP_BOUNDTYPE_UPPER);
      return SCIPvarGetUbchgInfo(var, bdchgidx, after);
   }
}

/** returns lower bound of variable directly before or after the bound change given by the bound change index
 *  was applied
 */
SCIP_Real SCIPvarGetLbAtIndex(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   assert(var != NULL);

   /* get bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      return SCIPvarGetLbAtIndex(var->data.original.transvar, bdchgidx, after);
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      if( bdchgidx == NULL )
         return SCIPvarGetLbLocal(var);
      else
      {
         SCIP_BDCHGINFO* bdchginfo;
         
         bdchginfo = SCIPvarGetLbchgInfo(var, bdchgidx, after);
         if( bdchginfo != NULL )
            return SCIPbdchginfoGetNewbound(bdchginfo);
         else
            return var->glbdom.lb;
      }

   case SCIP_VARSTATUS_FIXED:
      return var->glbdom.lb;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( var->data.aggregate.scalar > 0.0 )
      {
         /* a > 0 -> get lower bound of y */
         return var->data.aggregate.scalar * SCIPvarGetLbAtIndex(var->data.aggregate.var, bdchgidx, after)
            + var->data.aggregate.constant;
      }
      else if( var->data.aggregate.scalar < 0.0 )
      {
         /* a < 0 -> get upper bound of y */
         return var->data.aggregate.scalar * SCIPvarGetUbAtIndex(var->data.aggregate.var, bdchgidx, after)
            + var->data.aggregate.constant;
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         SCIPABORT();
         return 0.0;
      }
      
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo get the sides of the corresponding linear constraint */
      SCIPerrorMessage("getting the bounds of a multiple aggregated variable is not implemented yet\n");
      SCIPABORT();
      return 0.0;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetUbAtIndex(var->negatedvar, bdchgidx, after);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns upper bound of variable directly before or after the bound change given by the bound change index
 *  was applied
 */
SCIP_Real SCIPvarGetUbAtIndex(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   assert(var != NULL);

   /* get bounds of attached variables */
   switch( SCIPvarGetStatus(var) )
   {
   case SCIP_VARSTATUS_ORIGINAL:
      assert(var->data.original.transvar != NULL);
      return SCIPvarGetUbAtIndex(var->data.original.transvar, bdchgidx, after);
         
   case SCIP_VARSTATUS_COLUMN:
   case SCIP_VARSTATUS_LOOSE:
      if( bdchgidx == NULL )
         return SCIPvarGetUbLocal(var);
      else
      {
         SCIP_BDCHGINFO* bdchginfo;
         
         bdchginfo = SCIPvarGetUbchgInfo(var, bdchgidx, after);
         if( bdchginfo != NULL )
            return SCIPbdchginfoGetNewbound(bdchginfo);
         else
            return var->glbdom.ub;
      }

   case SCIP_VARSTATUS_FIXED:
      return var->glbdom.ub;
      
   case SCIP_VARSTATUS_AGGREGATED: /* x = a*y + c  ->  y = (x-c)/a */
      assert(var->data.aggregate.var != NULL);
      if( var->data.aggregate.scalar > 0.0 )
      {
         /* a > 0 -> get lower bound of y */
         return var->data.aggregate.scalar * SCIPvarGetUbAtIndex(var->data.aggregate.var, bdchgidx, after)
            + var->data.aggregate.constant;
      }
      else if( var->data.aggregate.scalar < 0.0 )
      {
         /* a < 0 -> get upper bound of y */
         return var->data.aggregate.scalar * SCIPvarGetLbAtIndex(var->data.aggregate.var, bdchgidx, after)
            + var->data.aggregate.constant;
      }
      else
      {
         SCIPerrorMessage("scalar is zero in aggregation\n");
         SCIPABORT();
         return 0.0;
      }
      
   case SCIP_VARSTATUS_MULTAGGR:
      /**@todo get the sides of the corresponding linear constraint */
      SCIPerrorMessage("getting the bounds of a multiple aggregated variable is not implemented yet\n");
      SCIPABORT();
      return 0.0;
      
   case SCIP_VARSTATUS_NEGATED: /* x' = offset - x  ->  x = offset - x' */
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar->negatedvar == var);
      return var->data.negate.constant - SCIPvarGetLbAtIndex(var->negatedvar, bdchgidx, after);
      
   default:
      SCIPerrorMessage("unknown variable status\n");
      SCIPABORT();
      return 0.0;
   }
}

/** returns lower or upper bound of variable directly before or after the bound change given by the bound change index
 *  was applied
 */
SCIP_Real SCIPvarGetBdAtIndex(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BOUNDTYPE        boundtype,          /**< type of bound: lower or upper bound */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   if( boundtype == SCIP_BOUNDTYPE_LOWER )
      return SCIPvarGetLbAtIndex(var, bdchgidx, after);
   else
   {
      assert(boundtype == SCIP_BOUNDTYPE_UPPER);
      return SCIPvarGetUbAtIndex(var, bdchgidx, after);
   }
}

/** returns whether the binary variable was fixed at the time given by the bound change index */
SCIP_Bool SCIPvarWasFixedAtIndex(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index representing time on path to current node */
   SCIP_Bool             after               /**< should the bound change with given index be included? */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetType(var) == SCIP_VARTYPE_BINARY);

   /* check the current bounds first in order to decide at which bound change information we have to look
    * (which is expensive because we have to follow the aggregation tree to the active variable)
    */
   return ((SCIPvarGetLbLocal(var) > 0.5 && SCIPvarGetLbAtIndex(var, bdchgidx, after) > 0.5)
      || (SCIPvarGetUbLocal(var) < 0.5 && SCIPvarGetUbAtIndex(var, bdchgidx, after) < 0.5));
}

/** bound change index representing the initial time before any bound changes took place */
static SCIP_BDCHGIDX initbdchgidx = {-2, 0};

/** bound change index representing the presolving stage */
static SCIP_BDCHGIDX presolvebdchgidx = {-1, 0};

/** returns the last bound change index, at which the bounds of the given variable were tightened */
SCIP_BDCHGIDX* SCIPvarGetLastBdchgIndex(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   SCIP_BDCHGIDX* lbchgidx;
   SCIP_BDCHGIDX* ubchgidx;

   assert(var != NULL);

   var = SCIPvarGetProbvar(var);

   /* check, if variable is original without transformed variable */
   if( var == NULL )
      return &initbdchgidx;

   /* check, if variable was fixed in presolving */
   if( !SCIPvarIsActive(var) )
      return &presolvebdchgidx;

   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);

   /* get depths of last bound change infos for the lower and upper bound */
   lbchgidx = (var->nlbchginfos > 0 ? &var->lbchginfos[var->nlbchginfos-1].bdchgidx : &initbdchgidx);
   ubchgidx = (var->nubchginfos > 0 ? &var->ubchginfos[var->nubchginfos-1].bdchgidx : &initbdchgidx);

   if( SCIPbdchgidxIsEarlierNonNull(lbchgidx, ubchgidx) )
      return ubchgidx;
   else
      return lbchgidx;
}

/** returns the last depth level, at which the bounds of the given variable were tightened;
 *  returns -2, if the variable's bounds are still the global bounds
 *  returns -1, if the variable was fixed in presolving
 */
int SCIPvarGetLastBdchgDepth(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   SCIP_BDCHGIDX* bdchgidx;

   bdchgidx = SCIPvarGetLastBdchgIndex(var);
   assert(bdchgidx != NULL);

   return bdchgidx->depth;
}

/** returns whether the first binary variable was fixed earlier than the second one;
 *  returns FALSE, if the first variable is not fixed, and returns TRUE, if the first variable is fixed, but the
 *  second one is not fixed
 */
SCIP_Bool SCIPvarWasFixedEarlier(
   SCIP_VAR*             var1,               /**< first binary variable */
   SCIP_VAR*             var2                /**< second binary variable */
   )
{
   SCIP_BDCHGIDX* bdchgidx1;
   SCIP_BDCHGIDX* bdchgidx2;

   assert(var1 != NULL);
   assert(var2 != NULL);
   assert(SCIPvarGetType(var1) == SCIP_VARTYPE_BINARY);
   assert(SCIPvarGetType(var2) == SCIP_VARTYPE_BINARY);

   var1 = SCIPvarGetProbvar(var1);
   var2 = SCIPvarGetProbvar(var2);
   assert(var1 != NULL);
   assert(var2 != NULL);

   /* check, if variables are globally fixed */
   if( !SCIPvarIsActive(var2) || var2->glbdom.lb > 0.5 || var2->glbdom.ub < 0.5 )
      return FALSE;
   if( !SCIPvarIsActive(var1) || var1->glbdom.lb > 0.5 || var1->glbdom.ub < 0.5 )
      return TRUE;

   assert(SCIPvarGetStatus(var1) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var1) == SCIP_VARSTATUS_COLUMN);
   assert(SCIPvarGetStatus(var2) == SCIP_VARSTATUS_LOOSE || SCIPvarGetStatus(var2) == SCIP_VARSTATUS_COLUMN);
   assert(SCIPvarGetType(var1) == SCIP_VARTYPE_BINARY);
   assert(SCIPvarGetType(var2) == SCIP_VARTYPE_BINARY);
   assert(var1->nlbchginfos + var1->nubchginfos <= 1);
   assert(var2->nlbchginfos + var2->nubchginfos <= 1);

   if( var1->nlbchginfos == 1 )
      bdchgidx1 = &var1->lbchginfos[0].bdchgidx;
   else if( var1->nubchginfos == 1 )
      bdchgidx1 = &var1->ubchginfos[0].bdchgidx;
   else
      bdchgidx1 = NULL;

   if( var2->nlbchginfos == 1 )
      bdchgidx2 = &var2->lbchginfos[0].bdchgidx;
   else if( var2->nubchginfos == 1 )
      bdchgidx2 = &var2->ubchginfos[0].bdchgidx;
   else
      bdchgidx2 = NULL;

   return SCIPbdchgidxIsEarlier(bdchgidx1, bdchgidx2);
}



/*
 * Hash functions
 */

/** gets the key (i.e. the name) of the given variable */
SCIP_DECL_HASHGETKEY(SCIPhashGetKeyVar)
{  /*lint --e{715}*/
   SCIP_VAR* var = (SCIP_VAR*)elem;

   assert(var != NULL);
   return var->name;
}




/*
 * simple functions implemented as defines
 */

/* In debug mode, the following methods are implemented as function calls to ensure
 * type validity.
 * In optimized mode, the methods are implemented as defines to improve performance.
 * However, we want to have them in the library anyways, so we have to undef the defines.
 */

#undef SCIPboundchgGetNewbound
#undef SCIPboundchgGetVar
#undef SCIPboundchgGetBoundchgtype
#undef SCIPboundchgGetBoundtype
#undef SCIPdomchgGetNBoundchgs
#undef SCIPdomchgGetBoundchg
#undef SCIPvarGetName
#undef SCIPvarGetData
#undef SCIPvarSetData
#undef SCIPvarSetDelorigData
#undef SCIPvarSetTransData
#undef SCIPvarSetDeltransData
#undef SCIPvarGetStatus
#undef SCIPvarIsOriginal
#undef SCIPvarIsTransformed
#undef SCIPvarIsNegated
#undef SCIPvarGetType
#undef SCIPvarIsIntegral
#undef SCIPvarIsInitial
#undef SCIPvarIsRemoveable
#undef SCIPvarIsDeleted
#undef SCIPvarIsActive
#undef SCIPvarGetIndex
#undef SCIPvarGetProbindex
#undef SCIPvarGetTransVar
#undef SCIPvarGetCol
#undef SCIPvarIsInLP
#undef SCIPvarGetAggrVar
#undef SCIPvarGetAggrScalar
#undef SCIPvarGetAggrConstant
#undef SCIPvarGetMultaggrNVars
#undef SCIPvarGetMultaggrVars
#undef SCIPvarGetMultaggrScalars
#undef SCIPvarGetMultaggrConstant
#undef SCIPvarGetNegatedVar
#undef SCIPvarGetNegationVar
#undef SCIPvarGetNegationConstant
#undef SCIPvarGetObj
#undef SCIPvarGetLbOriginal
#undef SCIPvarGetUbOriginal
#undef SCIPvarGetLbGlobal
#undef SCIPvarGetUbGlobal
#undef SCIPvarGetLbLocal
#undef SCIPvarGetUbLocal
#undef SCIPvarGetBranchFactor
#undef SCIPvarGetBranchPriority
#undef SCIPvarGetBranchDirection
#undef SCIPvarGetNVlbs
#undef SCIPvarGetVlbVars
#undef SCIPvarGetVlbCoefs
#undef SCIPvarGetVlbConstants
#undef SCIPvarGetNVubs
#undef SCIPvarGetVubVars
#undef SCIPvarGetVubCoefs
#undef SCIPvarGetVubConstants
#undef SCIPvarGetNImpls
#undef SCIPvarGetNBinImpls
#undef SCIPvarGetImplVars
#undef SCIPvarGetImplTypes
#undef SCIPvarGetImplBounds
#undef SCIPvarGetImplIds
#undef SCIPvarGetNCliques
#undef SCIPvarGetCliques
#undef SCIPvarCatchEvent
#undef SCIPvarDropEvent
#undef SCIPbdchgidxIsEarlierNonNull
#undef SCIPbdchgidxIsEarlier
#undef SCIPbdchginfoGetOldbound
#undef SCIPbdchginfoGetNewbound
#undef SCIPbdchginfoGetVar
#undef SCIPbdchginfoGetChgtype
#undef SCIPbdchginfoGetBoundtype
#undef SCIPbdchginfoGetDepth
#undef SCIPbdchginfoGetPos
#undef SCIPbdchginfoGetIdx
#undef SCIPbdchginfoGetInferVar
#undef SCIPbdchginfoGetInferCons
#undef SCIPbdchginfoGetInferProp
#undef SCIPbdchginfoGetInferInfo
#undef SCIPbdchginfoGetInferBoundtype
#undef SCIPbdchginfoHasInferenceReason

/** returns the new value of the bound in the bound change data */
SCIP_Real SCIPboundchgGetNewbound(
   SCIP_BOUNDCHG*        boundchg            /**< bound change data */
   )
{
   assert(boundchg != NULL);

   return boundchg->newbound;
}

/** returns the variable of the bound change in the bound change data */
SCIP_VAR* SCIPboundchgGetVar(
   SCIP_BOUNDCHG*        boundchg            /**< bound change data */
   )
{
   assert(boundchg != NULL);

   return boundchg->var;
}

/** returns the bound change type of the bound change in the bound change data */
SCIP_BOUNDCHGTYPE SCIPboundchgGetBoundchgtype(
   SCIP_BOUNDCHG*        boundchg            /**< bound change data */
   )
{
   assert(boundchg != NULL);

   return (SCIP_BOUNDCHGTYPE)(boundchg->boundchgtype);
}

/** returns the bound type of the bound change in the bound change data */
SCIP_BOUNDTYPE SCIPboundchgGetBoundtype(
   SCIP_BOUNDCHG*        boundchg            /**< bound change data */
   )
{
   assert(boundchg != NULL);

   return (SCIP_BOUNDTYPE)(boundchg->boundtype);
}

/** returns the number of bound changes in the domain change data */
int SCIPdomchgGetNBoundchgs(
   SCIP_DOMCHG*          domchg              /**< domain change data */
   )
{
   return domchg != NULL ? domchg->domchgbound.nboundchgs : 0;
}

/** returns a particular bound change in the domain change data */
SCIP_BOUNDCHG* SCIPdomchgGetBoundchg(
   SCIP_DOMCHG*          domchg,             /**< domain change data */
   int                   pos                 /**< position of the bound change in the domain change data */
   )
{
   assert(domchg != NULL);
   assert(0 <= pos && pos < domchg->domchgbound.nboundchgs);

   return &domchg->domchgbound.boundchgs[pos];
}

/** get name of variable */
const char* SCIPvarGetName(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->name;
}

/** returns the user data of the variable */
SCIP_VARDATA* SCIPvarGetData(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->vardata;
}

/** sets the user data for the variable */
void SCIPvarSetData(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_VARDATA*         vardata             /**< user variable data */
   )
{
   assert(var != NULL);

   var->vardata = vardata;
}

/** sets method to free user data for the original variable */
void SCIPvarSetDelorigData(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_DECL_VARDELORIG  ((*vardelorig))     /**< frees user data of original variable */
   )
{
   assert(var != NULL);
   assert(var->varstatus == SCIP_VARSTATUS_ORIGINAL);

   var->vardelorig = vardelorig;
}

/** sets method to transform user data of the variable */
void SCIPvarSetTransData(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_DECL_VARTRANS    ((*vartrans))       /**< creates transformed user data by transforming original user data */
   )
{
   assert(var != NULL);

   var->vartrans = vartrans;
}

/** sets method to free transformed user data for the variable */
void SCIPvarSetDeltransData(
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_DECL_VARDELTRANS ((*vardeltrans))    /**< frees user data of transformed variable */
   )
{
   assert(var != NULL);

   var->vardeltrans = vardeltrans;
}

/** gets status of variable */
SCIP_VARSTATUS SCIPvarGetStatus(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (SCIP_VARSTATUS)(var->varstatus);
}

/** returns whether the variable belongs to the original problem */
SCIP_Bool SCIPvarIsOriginal(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) != SCIP_VARSTATUS_NEGATED || var->negatedvar != NULL);

   return (SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL
      || (SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED
         && SCIPvarGetStatus(var->negatedvar) == SCIP_VARSTATUS_ORIGINAL));
}

/** returns whether the variable belongs to the transformed problem */
SCIP_Bool SCIPvarIsTransformed(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) != SCIP_VARSTATUS_NEGATED || var->negatedvar != NULL);

   return (SCIPvarGetStatus(var) != SCIP_VARSTATUS_ORIGINAL
      && (SCIPvarGetStatus(var) != SCIP_VARSTATUS_NEGATED
         || SCIPvarGetStatus(var->negatedvar) != SCIP_VARSTATUS_ORIGINAL));
}

/** returns whether the variable was created by negation of a different variable */
SCIP_Bool SCIPvarIsNegated(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);
}

/** gets type of variable */
SCIP_VARTYPE SCIPvarGetType(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (SCIP_VARTYPE)(var->vartype);
}

/** returns whether variable is of integral type (binary, integer, or implicit integer) */
SCIP_Bool SCIPvarIsIntegral(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (SCIPvarGetType(var) != SCIP_VARTYPE_CONTINUOUS);
}

/** returns whether variable's column should be present in the initial root LP */
SCIP_Bool SCIPvarIsInitial(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->initial;
}

/** returns whether variable's column is removeable from the LP (due to aging or cleanup) */
SCIP_Bool SCIPvarIsRemoveable(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->removeable;
}

/** returns whether the variable was deleted from the problem */
SCIP_Bool SCIPvarIsDeleted(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->deleted;
}

/** returns whether variable is an active (neither fixed nor aggregated) variable */
SCIP_Bool SCIPvarIsActive(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (var->probindex >= 0);
}

/** gets unique index of variable */
int SCIPvarGetIndex(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->index;
}

/** gets position of variable in problem, or -1 if variable is not active */
int SCIPvarGetProbindex(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->probindex;
}

/** gets transformed variable of ORIGINAL variable */
SCIP_VAR* SCIPvarGetTransVar(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL);

   return var->data.original.transvar;
}

/** gets column of COLUMN variable */
SCIP_COL* SCIPvarGetCol(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);

   return var->data.col;
}

/** returns whether the variable is a COLUMN variable that is member of the current LP */
SCIP_Bool SCIPvarIsInLP(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN && SCIPcolIsInLP(var->data.col));
}

/** gets aggregation variable y of an aggregated variable x = a*y + c */
SCIP_VAR* SCIPvarGetAggrVar(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_AGGREGATED);

   return var->data.aggregate.var;
}

/** gets aggregation scalar a of an aggregated variable x = a*y + c */
SCIP_Real SCIPvarGetAggrScalar(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_AGGREGATED);

   return var->data.aggregate.scalar;
}

/** gets aggregation constant c of an aggregated variable x = a*y + c */
SCIP_Real SCIPvarGetAggrConstant(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_AGGREGATED);

   return var->data.aggregate.constant;
}

/** gets number n of aggregation variables of a multi aggregated variable x = a0*y0 + ... + a(n-1)*y(n-1) + c */
int SCIPvarGetMultaggrNVars(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);

   return var->data.multaggr.nvars;
}

/** gets vector of aggregation variables y of a multi aggregated variable x = a0*y0 + ... + a(n-1)*y(n-1) + c */
SCIP_VAR** SCIPvarGetMultaggrVars(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);

   return var->data.multaggr.vars;
}

/** gets vector of aggregation scalars a of a multi aggregated variable x = a0*y0 + ... + a(n-1)*y(n-1) + c */
SCIP_Real* SCIPvarGetMultaggrScalars(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);

   return var->data.multaggr.scalars;
}

/** gets aggregation constant c of a multi aggregated variable x = a0*y0 + ... + a(n-1)*y(n-1) + c */
SCIP_Real SCIPvarGetMultaggrConstant(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);

   return var->data.multaggr.constant;
}

/** gets the negation of the given variable; may return NULL, if no negation is existing yet */
SCIP_VAR* SCIPvarGetNegatedVar(
   SCIP_VAR*             var                 /**< negated problem variable */
   )
{
   assert(var != NULL);

   return var->negatedvar;
}

/** gets the negation variable x of a negated variable x' = offset - x */
SCIP_VAR* SCIPvarGetNegationVar(
   SCIP_VAR*             var                 /**< negated problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);

   return var->negatedvar;
}

/** gets the negation offset of a negated variable x' = offset - x */
SCIP_Real SCIPvarGetNegationConstant(
   SCIP_VAR*             var                 /**< negated problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);

   return var->data.negate.constant;
}

/** gets objective function value of variable */
SCIP_Real SCIPvarGetObj(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->obj;
}
   
/** gets original lower bound of original problem variable (i.e. the bound set in problem creation) */
SCIP_Real SCIPvarGetLbOriginal(
   SCIP_VAR*             var                 /**< original problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsOriginal(var));

   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL )
      return var->data.original.origdom.lb;
   else
   {
      assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) == SCIP_VARSTATUS_ORIGINAL);

      return var->data.negate.constant - var->negatedvar->data.original.origdom.ub;
   }
}

/** gets original upper bound of original problem variable (i.e. the bound set in problem creation) */
SCIP_Real SCIPvarGetUbOriginal(
   SCIP_VAR*             var                 /**< original problem variable */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsOriginal(var));

   if( SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL )
      return var->data.original.origdom.ub;
   else
   {
      assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_NEGATED);
      assert(var->negatedvar != NULL);
      assert(SCIPvarGetStatus(var->negatedvar) == SCIP_VARSTATUS_ORIGINAL);

      return var->data.negate.constant - var->negatedvar->data.original.origdom.lb;
   }
}

/** gets global lower bound of variable */
SCIP_Real SCIPvarGetLbGlobal(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->glbdom.lb;
}

/** gets global upper bound of variable */
SCIP_Real SCIPvarGetUbGlobal(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->glbdom.ub;
}

/** gets current lower bound of variable */
SCIP_Real SCIPvarGetLbLocal(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->locdom.lb;
}
   
/** gets current upper bound of variable */
SCIP_Real SCIPvarGetUbLocal(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->locdom.ub;
}

/** gets the branch factor of the variable; this value can be used in the branching methods to scale the score
 *  values of the variables; higher factor leads to a higher probability that this variable is chosen for branching
 */
SCIP_Real SCIPvarGetBranchFactor(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->branchfactor;
}

/** gets the branch priority of the variable; variables with higher priority should always be preferred to variables
 *  with lower priority
 */
int SCIPvarGetBranchPriority(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return var->branchpriority;
}

/** gets the preferred branch direction of the variable (downwards, upwards, or auto) */
SCIP_BRANCHDIR SCIPvarGetBranchDirection(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return (SCIP_BRANCHDIR)var->branchdirection;
}

/** gets number of variable lower bounds x >= b_i*z_i + d_i of given variable x */
int SCIPvarGetNVlbs(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetNVbds(var->vlbs);
}

/** gets array with bounding variables z_i in variable lower bounds x >= b_i*z_i + d_i of given variable x;
 *  the variable bounds are sorted by increasing variable index of the bounding variable z_i (see SCIPvarGetIndex())
 */
SCIP_VAR** SCIPvarGetVlbVars(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetVars(var->vlbs);
}

/** gets array with bounding coefficients b_i in variable lower bounds x >= b_i*z_i + d_i of given variable x */
SCIP_Real* SCIPvarGetVlbCoefs(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetCoefs(var->vlbs);
}

/** gets array with bounding constants d_i in variable lower bounds x >= b_i*z_i + d_i of given variable x */
SCIP_Real* SCIPvarGetVlbConstants(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetConstants(var->vlbs);
}

/** gets number of variable upper bounds x <= b_i*z_i + d_i of given variable x */
int SCIPvarGetNVubs(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetNVbds(var->vubs);
}

/** gets array with bounding variables z_i in variable upper bounds x <= b_i*z_i + d_i of given variable x;
 *  the variable bounds are sorted by increasing variable index of the bounding variable z_i (see SCIPvarGetIndex())
 */
SCIP_VAR** SCIPvarGetVubVars(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetVars(var->vubs);
}

/** gets array with bounding coefficients b_i in variable upper bounds x <= b_i*z_i + d_i of given variable x */
SCIP_Real* SCIPvarGetVubCoefs(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetCoefs(var->vubs);
}

/** gets array with bounding constants d_i in variable upper bounds x <= b_i*z_i + d_i of given variable x */
SCIP_Real* SCIPvarGetVubConstants(
   SCIP_VAR*             var                 /**< problem variable */
   )
{
   assert(var != NULL);

   return SCIPvboundsGetConstants(var->vubs);
}

/** gets number of implications  y <= b or y >= b for x == 0 or x == 1 of given active problem variable x, 
 *  there are no implications for nonbinary variable x
 */
int SCIPvarGetNImpls(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for implications for x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPimplicsGetNImpls(var->implics, varfixing);
}

/** gets number of implications  y <= 0 or y >= 1 for x == 0 or x == 1 of given active problem variable x with binary y, 
 *  there are no implications for nonbinary variable x
 */
int SCIPvarGetNBinImpls(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for implications for x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPimplicsGetNBinImpls(var->implics, varfixing);
}

/** gets array with implication variables y of implications  y <= b or y >= b for x == 0 or x == 1 of given active
 *  problem variable x, there are no implications for nonbinary variable x;
 *  the implications are sorted such that implications with binary implied variables precede the ones with non-binary
 *  implied variables, and as a second criteria, the implied variables are sorted by increasing variable index
 *  (see SCIPvarGetIndex())
 */
SCIP_VAR** SCIPvarGetImplVars(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for implications for x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPimplicsGetVars(var->implics, varfixing);
}

/** gets array with implication types of implications  y <= b or y >= b for x == 0 or x == 1 of given active problem
 *  variable x (SCIP_BOUNDTYPE_UPPER if y <= b, SCIP_BOUNDTYPE_LOWER if y >= b), 
 *  there are no implications for nonbinary variable x
 */
SCIP_BOUNDTYPE* SCIPvarGetImplTypes(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for implications for x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPimplicsGetTypes(var->implics, varfixing);
}

/** gets array with implication bounds b of implications  y <= b or y >= b for x == 0 or x == 1 of given active problem
 *  variable x, there are no implications for nonbinary variable x
 */
SCIP_Real* SCIPvarGetImplBounds(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for implications for x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPimplicsGetBounds(var->implics, varfixing);
}

/** gets array with unique ids of implications  y <= b or y >= b for x == 0 or x == 1 of given active problem variable x,  
 *  there are no implications for nonbinary variable x
 */
int* SCIPvarGetImplIds(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for implications for x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPimplicsGetIds(var->implics, varfixing);
}

/** gets number of cliques, the active variable is contained in */
int SCIPvarGetNCliques(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for cliques containing x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPcliquelistGetNCliques(var->cliquelist, varfixing);
}

/** gets array of cliques, the active variable is contained in */
SCIP_CLIQUE** SCIPvarGetCliques(
   SCIP_VAR*             var,                /**< active problem variable */
   SCIP_Bool             varfixing           /**< FALSE for cliques containing x == 0, TRUE for x == 1 */
   )
{
   assert(var != NULL);
   assert(SCIPvarIsActive(var));

   return SCIPcliquelistGetCliques(var->cliquelist, varfixing);
}

/** includes event handler with given data in variable's event filter */
SCIP_RETCODE SCIPvarCatchEvent(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTTYPE        eventtype,          /**< event type to catch */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler to call for the event processing */
   SCIP_EVENTDATA*       eventdata,          /**< event data to pass to the event handler for the event processing */
   int*                  filterpos           /**< pointer to store position of event filter entry, or NULL */
   )
{
   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert((eventtype & ~SCIP_EVENTTYPE_VARCHANGED) == 0);
   assert((eventtype & SCIP_EVENTTYPE_VARCHANGED) != 0);
   assert(SCIPvarIsTransformed(var));

   SCIPdebugMessage("catch event of type 0x%x of variable <%s> with handler %p and data %p\n", 
      eventtype, var->name, eventhdlr, eventdata);

   SCIP_CALL( SCIPeventfilterAdd(var->eventfilter, blkmem, set, eventtype, eventhdlr, eventdata, filterpos) );

   return SCIP_OKAY;
}

/** deletes event handler with given data from variable's event filter */
SCIP_RETCODE SCIPvarDropEvent(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_EVENTTYPE        eventtype,          /**< event type mask of dropped event */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler to call for the event processing */
   SCIP_EVENTDATA*       eventdata,          /**< event data to pass to the event handler for the event processing */
   int                   filterpos           /**< position of event filter entry returned by SCIPvarCatchEvent(), or -1 */
   )
{
   assert(var != NULL);
   assert(var->eventfilter != NULL);
   assert(SCIPvarIsTransformed(var));

   SCIPdebugMessage("drop event of variable <%s> with handler %p and data %p\n", var->name, eventhdlr, eventdata);

   SCIP_CALL( SCIPeventfilterDel(var->eventfilter, blkmem, set, eventtype, eventhdlr, eventdata, filterpos) );

   return SCIP_OKAY;
}

/** returns whether first bound change index belongs to an earlier applied bound change than second one */
SCIP_Bool SCIPbdchgidxIsEarlierNonNull(
   SCIP_BDCHGIDX*        bdchgidx1,          /**< first bound change index */
   SCIP_BDCHGIDX*        bdchgidx2           /**< second bound change index */
   )
{
   assert(bdchgidx1 != NULL);
   assert(bdchgidx1->depth >= -2);
   assert(bdchgidx1->pos >= 0);
   assert(bdchgidx2 != NULL);
   assert(bdchgidx2->depth >= -2);
   assert(bdchgidx2->pos >= 0);

   return (bdchgidx1->depth < bdchgidx2->depth)
      || (bdchgidx1->depth == bdchgidx2->depth && (bdchgidx1->pos < bdchgidx2->pos));
}

/** returns whether first bound change index belongs to an earlier applied bound change than second one;
 *  if a bound change index is NULL, the bound change index represents the current time, i.e. the time after the
 *  last bound change was applied to the current node
 */
SCIP_Bool SCIPbdchgidxIsEarlier(
   SCIP_BDCHGIDX*        bdchgidx1,          /**< first bound change index, or NULL */
   SCIP_BDCHGIDX*        bdchgidx2           /**< second bound change index, or NULL */
   )
{
   assert(bdchgidx1 == NULL || bdchgidx1->depth >= -2);
   assert(bdchgidx1 == NULL || bdchgidx1->pos >= 0);
   assert(bdchgidx2 == NULL || bdchgidx2->depth >= -2);
   assert(bdchgidx2 == NULL || bdchgidx2->pos >= 0);

   if( bdchgidx1 == NULL )
      return FALSE;
   else if( bdchgidx2 == NULL )
      return TRUE;
   else
      return (bdchgidx1->depth < bdchgidx2->depth)
         || (bdchgidx1->depth == bdchgidx2->depth && (bdchgidx1->pos < bdchgidx2->pos));
}

/** returns old bound that was overwritten for given bound change information */
SCIP_Real SCIPbdchginfoGetOldbound(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return bdchginfo->oldbound;
}

/** returns new bound installed for given bound change information */
SCIP_Real SCIPbdchginfoGetNewbound(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return bdchginfo->newbound;
}

/** returns variable that belongs to the given bound change information */
SCIP_VAR* SCIPbdchginfoGetVar(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return bdchginfo->var;
}

/** returns whether the bound change information belongs to a branching decision or a deduction */
SCIP_BOUNDCHGTYPE SCIPbdchginfoGetChgtype(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return (SCIP_BOUNDCHGTYPE)(bdchginfo->boundchgtype);
}

/** returns whether the bound change information belongs to a lower or upper bound change */
SCIP_BOUNDTYPE SCIPbdchginfoGetBoundtype(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return (SCIP_BOUNDTYPE)(bdchginfo->boundtype);
}

/** returs depth level of given bound change information */
int SCIPbdchginfoGetDepth(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return bdchginfo->bdchgidx.depth;
}

/** returs bound change position in its depth level of given bound change information */
int SCIPbdchginfoGetPos(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return bdchginfo->bdchgidx.pos;
}

/** returs bound change index of given bound change information */
SCIP_BDCHGIDX* SCIPbdchginfoGetIdx(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return &bdchginfo->bdchgidx;
}

/** returs inference variable of given bound change information */
SCIP_VAR* SCIPbdchginfoGetInferVar(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);
   assert((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER
      || (SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER);

   return bdchginfo->inferencedata.var;
}

/** returs inference constraint of given bound change information */
SCIP_CONS* SCIPbdchginfoGetInferCons(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);
   assert((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER);
   assert(bdchginfo->inferencedata.reason.cons != NULL);

   return bdchginfo->inferencedata.reason.cons;
}

/** returs inference propagator of given bound change information, or NULL if no propagator was responsible */
SCIP_PROP* SCIPbdchginfoGetInferProp(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);
   assert((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER);

   return bdchginfo->inferencedata.reason.prop;
}

/** returs inference user information of given bound change information */
int SCIPbdchginfoGetInferInfo(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);
   assert((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER
      || (SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER);

   return bdchginfo->inferencedata.info;
}

/** returs inference bound of inference variable of given bound change information */
SCIP_BOUNDTYPE SCIPbdchginfoGetInferBoundtype(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);
   assert((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER
      || (SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER);

   return (SCIP_BOUNDTYPE)(bdchginfo->inferboundtype);
}

/** returns whether the bound change has an inference reason (constraint or propagator), that can be resolved */
SCIP_Bool SCIPbdchginfoHasInferenceReason(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change information */
   )
{
   assert(bdchginfo != NULL);

   return ((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_CONSINFER)
      || ((SCIP_BOUNDCHGTYPE)bdchginfo->boundchgtype == SCIP_BOUNDCHGTYPE_PROPINFER
         && bdchginfo->inferencedata.reason.prop != NULL);
}

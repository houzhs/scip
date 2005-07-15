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
#pragma ident "@(#) $Id: struct_relax.h,v 1.8 2005/07/15 17:20:20 bzfpfend Exp $"

/**@file   struct_relax.h
 * @brief  datastructures for relaxators
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_STRUCT_RELAX_H__
#define __SCIP_STRUCT_RELAX_H__


#include "scip/def.h"
#include "scip/type_clock.h"
#include "scip/type_relax.h"


/** relaxators data */
struct Relax
{
   Longint          ncalls;             /**< number of times, this relaxator was called */
   Longint          lastsolvednode;     /**< last total nodes counter, where the current relaxation was solved */
   char*            name;               /**< name of relaxator */
   char*            desc;               /**< description of relaxator */
   DECL_RELAXFREE   ((*relaxfree));     /**< destructor of relaxator */
   DECL_RELAXINIT   ((*relaxinit));     /**< initialize relaxator */
   DECL_RELAXEXIT   ((*relaxexit));     /**< deinitialize relaxator */
   DECL_RELAXINITSOL((*relaxinitsol));  /**< solving process initialization method of relaxator */
   DECL_RELAXEXITSOL((*relaxexitsol));  /**< solving process deinitialization method of relaxator */
   DECL_RELAXEXEC   ((*relaxexec));     /**< execution method of relaxator */
   RELAXDATA*       relaxdata;          /**< relaxators local data */
   CLOCK*           clock;              /**< relaxation time */
   int              priority;           /**< priority of the relaxator */
   int              freq;               /**< frequency for calling relaxator */
   Bool             initialized;        /**< is relaxator initialized? */
};


#endif

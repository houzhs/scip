/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2018 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   struct_benderscut.h
 * @ingroup INTERNALAPI
 * @brief  datastructures for Benders' decomposition cuts techniques
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_STRUCT_BENDERSCUT_H__
#define __SCIP_STRUCT_BENDERSCUT_H__


#include "scip/def.h"
#include "scip/type_clock.h"
#include "scip/type_benderscut.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SCIP_BenderscutCut
{
   SCIP_VAR**            vars;               /**< the variables forming the cut */
   SCIP_Real*            vals;               /**< the coefficients of the variables in the cut */
   int                   nvars;              /**< the number of variables in the cut */
   SCIP_Real             lhs;                /**< the left hand side of the cut */
   SCIP_Real             rhs;                /**< the right hand side of the cut */
};
typedef struct SCIP_BenderscutCut SCIP_BENDERSCUTCUT;

/** Benders' decomposition cuts data */
struct SCIP_Benderscut
{
   SCIP_Longint          ncalls;             /**< number of times, this Benders' decomposition cut was called */
   SCIP_Longint          nfound;             /**< number of cuts found so far by this Benders' decomposition cut */
   char*                 name;               /**< name of the Benders' decomposition cut */
   char*                 desc;               /**< description of the Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTCOPY((*benderscutcopy));/**< copy method of the Benders' decomposition cut or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_BENDERSCUTFREE((*benderscutfree));/**< destructor of the Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTINIT((*benderscutinit));/**< initialize the Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTEXIT((*benderscutexit));/**< deinitialize the Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTINITSOL((*benderscutinitsol));/**< solving process initialization method of the Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTEXITSOL((*benderscutexitsol));/**< solving process deinitialization method of the Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTEXEC((*benderscutexec));/**< execution method of the Benders' decomposition cut */
   SCIP_BENDERSCUTDATA*  benderscutdata;     /**< Benders' decomposition cuts local data */
   SCIP_CLOCK*           setuptime;          /**< time spend for setting up the Benders' decomposition cut plugin */
   SCIP_CLOCK*           benderscutclock;    /**< the execution time of the Benders' decomposition cut plugin */
   int                   priority;           /**< priority of the Benders' decomposition cuts */
   SCIP_Bool             islpcut;            /**< does this Benders' cut use LP information? */
   SCIP_Bool             initialized;        /**< has the Benders' decomposition cut been initialized? */

   SCIP_BENDERSCUTCUT**  addedcuts;          /**< array to store the data required to form a cut/constraint */
   int                   addedcutssize;      /**< the size of the added cuts array */
   int                   naddedcuts;         /**< the number of the added cuts */

   /* additional Benders' decomposition cuts parameters */
   SCIP_Bool             enabled;            /**< is this Benders' decomposition cut enabled? */
};

#ifdef __cplusplus
}
#endif

#endif

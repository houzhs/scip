/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2003 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2003 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: heur_objhistdiving.h,v 1.1 2004/01/26 15:10:16 bzfpfend Exp $"

/**@file   heur_objhistdiving.h
 * @brief  LP diving heuristic that changes variable's objective value instead of bounds, using history values as guide
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __HEUR_OBJHISTDIVING_H__
#define __HEUR_OBJHISTDIVING_H__


#include "scip.h"


/** creates the objhistdiving heuristic and includes it in SCIP */
extern
RETCODE SCIPincludeHeurObjhistdiving(
   SCIP*            scip                /**< SCIP data structure */
   );

#endif

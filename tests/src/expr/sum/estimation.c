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
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   estimation.c
 * @brief  tests estimation of sums
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/expr_sum.c"
#include "../estimation.h"

Test(estimation, sum, .init = setup, .fini = teardown,
   .description = "test separation for a sum expression"
   )
{
   SCIP_EXPR* expr;
   SCIP_Real refpoint[2] = { 0., 0. };
   SCIP_INTERVAL bounds[2];
   SCIP_Real coefs[2];
   SCIP_Real constant;
   SCIP_Bool islocal;
   SCIP_Bool success;
   SCIP_Bool branchcand = TRUE;

   SCIP_CALL( SCIPcreateExprSum(scip, &expr, 0, NULL, NULL, 1.5, NULL, NULL) );
   SCIP_CALL( SCIPappendExprSumExpr(scip, expr, xexpr, 2.3) );
   SCIP_CALL( SCIPappendExprSumExpr(scip, expr, yexpr, -5.1) );

   SCIP_CALL( estimateSum(scip, expr, bounds, bounds, refpoint, TRUE, SCIP_INVALID, coefs, &constant, &islocal, &success, &branchcand) );

   cr_expect(success);
   cr_expect_eq(coefs[0], 2.3);
   cr_expect_eq(coefs[1], -5.1);
   cr_expect_eq(constant, 1.5);
   cr_expect_not(islocal);
   cr_expect_not(branchcand);

   SCIP_CALL( SCIPreleaseExpr(scip, &expr) );
}

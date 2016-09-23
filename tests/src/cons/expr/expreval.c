/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2013 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   expreval.c
 * @brief  tests expression evaluation
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/scip.h"
#include "scip/cons_expr.h"
#include "scip/cons_expr_var.h"
#include "scip/cons_expr_value.h"
#include "scip/cons_expr_sum.h"
#include "scip/cons_expr_product.h"
#include "scip/cons_expr_pow.h"

#include "include/scip_test.h"

static SCIP* scip;
static SCIP_CONSHDLR* conshdlr;
static SCIP_SOL* sol;
static SCIP_VAR* x;
static SCIP_VAR* y;

/* creates scip, problem, includes expression constraint handler, creates  and adds variables */
static
void setup(void)
{
   SCIP_CALL( SCIPcreate(&scip) );

   /* include cons_expr: this adds the operator handlers */
   SCIP_CALL( SCIPincludeConshdlrExpr(scip) );

   /* currently expr constraints cannot be created */
   /* get expr conshdlr */
   conshdlr = SCIPfindConshdlr(scip, "expr");
   cr_assert(conshdlr != NULL);

   /* create problem */
   SCIP_CALL( SCIPcreateProbBasic(scip, "test_problem") );

   SCIP_CALL( SCIPcreateVarBasic(scip, &x, "x", 0.0, 1.0, 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPcreateVarBasic(scip, &y, "y", 0.0, 1.0, 0.0, SCIP_VARTYPE_CONTINUOUS) );
   SCIP_CALL( SCIPaddVar(scip, x) );
   SCIP_CALL( SCIPaddVar(scip, y) );

   /* create solution */
   SCIP_CALL( SCIPcreateSol(scip, &sol, NULL) );

   SCIP_CALL( SCIPsetRealParam(scip, "numerics/feastol", 1.0e-8) );
}

/* releases variables, frees scip */
static
void teardown(void)
{
   /* free allocated memory */
   SCIP_CALL( SCIPfreeSol(scip, &sol) );
   SCIP_CALL( SCIPreleaseVar(scip, &x) );
   SCIP_CALL( SCIPreleaseVar(scip, &y) );
   SCIP_CALL( SCIPfree(&scip) );

   cr_assert_eq(BMSgetMemoryUsed(), 0, "There is are memory leak!!");
}

TestSuite(evalexpr, .init = setup, .fini = teardown);

/** TESTS **/
Test(evalexpr, absolute, .description = "Tests expression evaluation for absolute expressions.")
{
   int i;
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_INTERVAL interval;
   const char* inputs[2] = {"abs(<x>[C]) + abs(<x>[C])",
      "abs(abs(abs(<x>[C]) + abs(<y>[C])) * abs(<x>[C])^3 * abs(<y>[C]))"};

   SCIP_CALL( (SCIPparseConsExprExpr(scip, conshdlr, (char*)inputs[0], NULL, &expr)) );
   SCIPinfoMessage(scip, NULL, "testing expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* evaluate expression for different points */
   for( i = -10; i <= 10; ++i )
   {
      /* evaluate expression */
      SCIP_CALL( SCIPsetSolVal(scip, sol, x, (SCIP_Real) i) );
      SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );

      cr_expect(SCIPisFeasEQ(scip, SCIPgetConsExprExprValue(expr),  2 * REALABS(i)));

      /* propagate expression */
      SCIP_CALL( SCIPchgVarLb(scip, x, -REALABS(i)) );
      SCIP_CALL( SCIPchgVarUb(scip, x, i*i) );
      SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, SCIPepsilon(scip)) );
      interval = SCIPgetConsExprExprInterval(expr);

      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), 0));
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetSup(interval), 2 * i*i));
   }
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /* test a more complicated expression */
   SCIP_CALL( (SCIPparseConsExprExpr(scip, conshdlr, (char*)inputs[1], NULL, &expr)) );
   SCIPinfoMessage(scip, NULL, "testing expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* evaluate expression for different points */
   for( i = -10; i <= 10; ++i )
   {
      /* evaluate expression */
      SCIP_CALL( SCIPsetSolVal(scip, sol, x, (SCIP_Real) i) );
      SCIP_CALL( SCIPsetSolVal(scip, sol, y, (SCIP_Real) i) );
      SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );
      cr_expect(SCIPisFeasEQ(scip, SCIPgetConsExprExprValue(expr), 2 * pow(REALABS(i),5)));

      /* propagate expression */
      SCIP_CALL( SCIPchgVarLb(scip, x, -REALABS(i)) );
      SCIP_CALL( SCIPchgVarUb(scip, x, REALABS(i)) );
      SCIP_CALL( SCIPchgVarLb(scip, y, -REALABS(i)) );
      SCIP_CALL( SCIPchgVarUb(scip, y, REALABS(i)) );
      SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, SCIPepsilon(scip)) );
      interval = SCIPgetConsExprExprInterval(expr);
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), 0));
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetSup(interval), 2 * pow(REALABS(i),5)));
   }
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

Test(evalexpr, exponential, .description = "Tests expression evaluation for exponential expressions.")
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_INTERVAL interval;
   const char* inputs[2] = {"exp(<x>[C]) + exp(<x>[C])", "exp(exp(<x>[C])) * exp(<y>[C])^2"};
   int i;

   SCIP_CALL( (SCIPparseConsExprExpr(scip, conshdlr, (char*)inputs[0], NULL, &expr)) );
   SCIPinfoMessage(scip, NULL, "testing expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* evaluate expression for different points */
   for( i = -10; i <= 10; ++i )
   {
      /* evaluate expression */
      SCIP_CALL( SCIPsetSolVal(scip, sol, x, (SCIP_Real) i) );
      SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );
      cr_expect(SCIPisFeasEQ(scip, SCIPgetConsExprExprValue(expr), exp(i) + exp(i)));

      /* propagate expression */
      SCIP_CALL( SCIPchgVarLb(scip, x, i) );
      SCIP_CALL( SCIPchgVarUb(scip, x, i + 1.0 / (ABS(i) + 1)) );
      SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, SCIPepsilon(scip)) );
      interval = SCIPgetConsExprExprInterval(expr);
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), 2*exp(i)));
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetSup(interval), 2*exp(i + 1.0 / (ABS(i) + 1))));
   }
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

   /* complicated exponential expression */
   SCIP_CALL( (SCIPparseConsExprExpr(scip, conshdlr, (char*)inputs[1], NULL, &expr)) );
   SCIPinfoMessage(scip, NULL, "testing expression: ");
   SCIP_CALL( SCIPprintConsExprExpr(scip, expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");

   /* evaluate expression for different points */
   for( i = 1; i <= 10; ++i )
   {
      /* evaluate expression */
      SCIP_CALL( SCIPsetSolVal(scip, sol, x, (SCIP_Real) 1.0 / i) );
      SCIP_CALL( SCIPsetSolVal(scip, sol, y, (SCIP_Real) i) );
      SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );
      cr_expect(SCIPisFeasEQ(scip, SCIPgetConsExprExprValue(expr), exp(exp(1.0 / i)) * exp(2*i)));

      /* propagate expression */
      SCIP_CALL( SCIPchgVarLb(scip, x, -1.0 / i) );
      SCIP_CALL( SCIPchgVarUb(scip, x,  1.0 / i) );
      SCIP_CALL( SCIPchgVarLb(scip, y, i) );
      SCIP_CALL( SCIPchgVarUb(scip, y, i + 1.0 / i) );
      SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, SCIPepsilon(scip)) );
      interval = SCIPgetConsExprExprInterval(expr);
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), exp(exp(-1.0 / i)) * exp(2*i)));
      cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetSup(interval), exp(exp(1.0 / i)) * exp(2*i + 2.0 / i)));
   }
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

Test(evalexpr, logarithm, .description = "Tests expression evaluation for logarithmic expressions.")
{
      SCIP_CONSEXPR_EXPR* expr;
      SCIP_INTERVAL interval;
      const char* inputs[2] = {"log(<x>[C]) + log(<x>[C])", "log(log(exp(<x>[C]) * exp(<y>[C])))"};
      int i;

      SCIP_CALL( (SCIPparseConsExprExpr(scip, conshdlr, (char*)inputs[0], NULL, &expr)) );
      SCIPinfoMessage(scip, NULL, "testing expression: ");
      SCIP_CALL( SCIPprintConsExprExpr(scip, expr, NULL) );
      SCIPinfoMessage(scip, NULL, "\n");

      /* evaluate expression for different points */
      for( i = -10; i <= 10; ++i )
      {
         SCIP_Real xlb, xub;

         /* evaluate expression */
         SCIP_CALL( SCIPsetSolVal(scip, sol, x, (SCIP_Real) i) );
         SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );

         if( i <= 0 )
            cr_expect_eq(SCIPgetConsExprExprValue(expr), SCIP_INVALID);
         else
            cr_expect(SCIPisFeasEQ(scip, SCIPgetConsExprExprValue(expr), log(i) + log(i)));

         /* propagate expression */
         xlb = i;
         xub = i + 1.0 / (ABS(i) + 1);
         SCIP_CALL( SCIPchgVarLb(scip, x, xlb) );
         SCIP_CALL( SCIPchgVarUb(scip, x, xub) );
         SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, SCIPepsilon(scip)) );
         interval = SCIPgetConsExprExprInterval(expr);

         /* interval is empty if both bounds are non-positive */
         if( xub <= 0 )
            cr_expect(SCIPintervalIsEmpty(SCIPinfinity(scip), interval));
         else
         {
            cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetSup(interval), 2*log(xub)));

            if( xlb <= 0 )
               cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), -SCIPinfinity(scip)));
            else
               cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), 2*log(xlb)));
         }
      }
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );

      /* complicated logarithmic expression */
      SCIP_CALL( (SCIPparseConsExprExpr(scip, conshdlr, (char*)inputs[1], NULL, &expr)) );
      SCIPinfoMessage(scip, NULL, "testing expression: ");
      SCIP_CALL( SCIPprintConsExprExpr(scip, expr, NULL) );
      SCIPinfoMessage(scip, NULL, "\n");

      /* evaluate expression for different points */
      for( i = 1; i <= 10; ++i )
      {
         /* evaluate expression */
         SCIP_CALL( SCIPsetSolVal(scip, sol, x, (SCIP_Real) i) );
         SCIP_CALL( SCIPsetSolVal(scip, sol, y, (SCIP_Real) i + 1) );
         SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );
         cr_expect(SCIPisFeasEQ(scip, SCIPgetConsExprExprValue(expr), log(2*i + 1) ));

         /* propagate expression */
         SCIP_CALL( SCIPchgVarLb(scip, x,  1.0 / i) );
         SCIP_CALL( SCIPchgVarUb(scip, x,  2.0 / i) );
         SCIP_CALL( SCIPchgVarLb(scip, y,  3.0 / i) );
         SCIP_CALL( SCIPchgVarUb(scip, y,  4.0 / i) );
         SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, SCIPepsilon(scip)) );
         interval = SCIPgetConsExprExprInterval(expr);
         cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetInf(interval), log(1.0 / i + 3.0 / i)));
         cr_expect(SCIPisFeasEQ(scip, SCIPintervalGetSup(interval), log(2.0 / i + 4.0 / i)));
      }

      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
}

Test(evalexpr, power, .description = "Tests expression evaluation for power expressions.")
{
   SCIP_CONSEXPR_EXPR* expr;
   SCIP_CONSEXPR_EXPR* xexpr;
   SCIP_INTERVAL interval;

   SCIP_CALL( SCIPchgVarLb(scip, x, -10.0) );
   SCIP_CALL( SCIPchgVarUb(scip, x, 10.0) );

   /* create expressions */
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, &xexpr, x) );
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &expr, xexpr, 2.5) );

   /*
    * evaluate expression
    */
   SCIP_CALL( SCIPsetSolVal(scip, sol, x, 2.0) );
   SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );
   cr_assert(SCIPisEQ(scip, SCIPgetConsExprExprValue(expr), pow(2.0, 2.5)));

   /* evaluate expression for an undefined point */
   SCIP_CALL( SCIPsetSolVal(scip, sol, x, -1.0) );
   SCIP_CALL( SCIPevalConsExprExpr(scip, expr, sol, 0) );
   cr_assert(SCIPgetConsExprExprValue(expr) == SCIP_INVALID);

   /*
    * propagate expression
    */
   SCIP_CALL( SCIPchgVarLb(scip, x, 1.0) );
   SCIP_CALL( SCIPchgVarUb(scip, x, 3.0) );
   SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, 0.0) );
   interval = SCIPgetConsExprExprInterval(expr);
   cr_assert(SCIPisEQ(scip, interval.inf, 1.0));
   cr_assert(SCIPisEQ(scip, interval.sup, pow(3.0, 2.5)));

   /* expression is not defined for the whole interval => resulting interval should be empty */
   SCIP_CALL( SCIPchgVarLb(scip, x, -2.0) );
   SCIP_CALL( SCIPchgVarUb(scip, x, -1.0) );
   SCIP_CALL( SCIPevalConsExprExprInterval(scip, expr, FALSE, 0, 0.0) );
   interval = SCIPgetConsExprExprInterval(expr);
   cr_assert(SCIPintervalIsEmpty(SCIPinfinity(scip), interval));

   /* free expressions */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &xexpr) );
}

/* creates expression for f(x,y) = 0.5 * ( (x^2*y^(-1)*5^(-4))^2 * (2*x + 1)^(-1) ) */
static
SCIP_RETCODE createExpr(
   SCIP_CONSEXPR_EXPR**  xexpr,              /**< pointer to store variable expression */
   SCIP_CONSEXPR_EXPR**  yexpr,              /**< pointer to store variable expression */
   SCIP_CONSEXPR_EXPR**  const_expr,         /**< pointer to store constant expression */
   SCIP_CONSEXPR_EXPR**  prodexpr,           /**< pointer to store product expression */
   SCIP_CONSEXPR_EXPR**  sumexpr,            /**< pointer to store sum expression */
   SCIP_CONSEXPR_EXPR**  mainexpr            /**< pointer to store full expression */
   )
{
   SCIP_CONSEXPR_EXPR* exprs[] = {NULL, NULL, NULL};
   SCIP_Real coef = 2.0;

   /* create variable and constant expressions */
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, xexpr, x) );
   SCIP_CALL( SCIPcreateConsExprExprVar(scip, conshdlr, yexpr, y) );
   SCIP_CALL( SCIPcreateConsExprExprValue(scip, conshdlr, const_expr, 5.0) );

   /* create sum expression */
   SCIP_CALL( SCIPcreateConsExprExprSum(scip, conshdlr, sumexpr, 1, xexpr, &coef, 1.0) );  /* 2*x+1 */

   /* create product expression */
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &exprs[0], *xexpr,  2.0) );  /* x^2 */
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &exprs[1], *yexpr, -1.0) );  /* y^(-1) */
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &exprs[2], *const_expr, -4.0) );  /* 5^(-4) */
   SCIP_CALL( SCIPcreateConsExprExprProduct(scip, conshdlr, prodexpr, 3, exprs, 1.0) );  /* x^2*y^(-1)*5^(-4) */

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &exprs[0]) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &exprs[1]) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &exprs[2]) );

   /* create main expression */
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &exprs[0], *prodexpr,  2.0) );  /* (x^2*y^(-1)*5^(-4))^2 */
   SCIP_CALL( SCIPcreateConsExprExprPow(scip, conshdlr, &exprs[1], *sumexpr, -1.0) );  /* (2*x + 1)^(-1) */
   SCIP_CALL( SCIPcreateConsExprExprProduct(scip, conshdlr, mainexpr, 2, exprs, 0.5) );

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &exprs[0]) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &exprs[1]) );

   return SCIP_OKAY;
}

/* helper function to check evaluation of expression created with createExpr() */
static
void checkExprEval(
   SCIP_CONSEXPR_EXPR*  xexpr,               /**< variable expression */
   SCIP_CONSEXPR_EXPR*  yexpr,               /**< variable expression */
   SCIP_CONSEXPR_EXPR*  const_expr,          /**< constant expression */
   SCIP_CONSEXPR_EXPR*  prodexpr,            /**< product expression */
   SCIP_CONSEXPR_EXPR*  sumexpr,             /**< sum expression */
   SCIP_CONSEXPR_EXPR*  mainexpr,            /**< full expression */
   SCIP_Real            xval,                /**< x value used for evaluation */
   SCIP_Real            yval,                /**< y value used for evaluation */
   unsigned int         tag                  /**< tag used for evaluation */
   )
{
   SCIP_Real prodval;
   SCIP_Real sumval;

   prodval = pow(xval,2)*pow(yval,-1)*pow(5,-4);
   sumval = 2*xval + 1;

   /* check values */
   cr_expect_float_eq(SCIPgetConsExprExprValue(mainexpr), 0.5 * pow(prodval,2) * pow(sumval,-1), SCIPepsilon(scip));
   cr_expect_float_eq(SCIPgetConsExprExprValue(sumexpr), sumval, SCIPepsilon(scip));
   cr_expect_float_eq(SCIPgetConsExprExprValue(prodexpr), prodval, SCIPepsilon(scip));
   cr_expect_float_eq(SCIPgetConsExprExprValue(xexpr), xval, SCIPepsilon(scip));
   cr_expect_float_eq(SCIPgetConsExprExprValue(yexpr), yval, SCIPepsilon(scip));
   cr_expect_float_eq(SCIPgetConsExprExprValue(const_expr), 5.0, SCIPepsilon(scip));

   /* check tags */
   cr_expect_eq(SCIPgetConsExprExprEvalTag(mainexpr), tag);
   cr_expect_eq(SCIPgetConsExprExprEvalTag(sumexpr), tag);
   cr_expect_eq(SCIPgetConsExprExprEvalTag(prodexpr), tag);
   cr_expect_eq(SCIPgetConsExprExprEvalTag(xexpr), tag);
   cr_expect_eq(SCIPgetConsExprExprEvalTag(yexpr), tag);
   cr_expect_eq(SCIPgetConsExprExprEvalTag(const_expr), tag);
}

Test(evalexpr, complicated, .description = "Tests expression evaluation for a large complicated expression.")
{
   SCIP_CONSEXPR_EXPR* xexpr;
   SCIP_CONSEXPR_EXPR* yexpr;
   SCIP_CONSEXPR_EXPR* const_expr;
   SCIP_CONSEXPR_EXPR* prodexpr;
   SCIP_CONSEXPR_EXPR* sumexpr;
   SCIP_CONSEXPR_EXPR* mainexpr;
   SCIP_CONSEXPR_PRINTDOTDATA* dotdata;
   int i;

   /* create all expressions */
   SCIP_CALL( createExpr(&xexpr, &yexpr, &const_expr, &prodexpr, &sumexpr, &mainexpr) );

   /* initialize solution values */
   SCIP_CALL( SCIPsetSolVal(scip, sol, x, 2.0) );
   SCIP_CALL( SCIPsetSolVal(scip, sol, y, 4.0) );

   /* evaluate main expression, print it, and check values for sub-expressions */
   printf("evaluate and check expressions\n");
   SCIP_CALL( SCIPevalConsExprExpr(scip, mainexpr, sol, 1) );
   SCIP_CALL( SCIPprintConsExprExprDotInit(scip, &dotdata, NULL, SCIP_CONSEXPR_PRINTDOT_EXPRSTRING | SCIP_CONSEXPR_PRINTDOT_EVALTAG) );
   SCIP_CALL( SCIPprintConsExprExprDot(scip, dotdata, mainexpr) );
   SCIP_CALL( SCIPprintConsExprExprDotFinal(scip, &dotdata) );
   checkExprEval(xexpr, yexpr, const_expr, prodexpr, sumexpr, mainexpr, 2.0, 4.0, 1);

   /* modify solution and evaluate expression with the same tag again; values should not change */
   printf("evaluate and check expressions with a modified solution but the same tag\n");
   SCIP_CALL( SCIPsetSolVal(scip, sol, x, -2.0) );
   SCIP_CALL( SCIPsetSolVal(scip, sol, y, -5.0) );
   SCIP_CALL( SCIPevalConsExprExpr(scip, mainexpr, sol, 1) );
   checkExprEval(xexpr, yexpr, const_expr, prodexpr, sumexpr, mainexpr, 2.0, 4.0, 1);

   /* evaluate expression with a different tag; values should have changed */
   printf("evaluate expression with a new tag\n");
   SCIP_CALL( SCIPevalConsExprExpr(scip, mainexpr, sol, 2) );
   checkExprEval(xexpr, yexpr, const_expr, prodexpr, sumexpr, mainexpr, -2.0, -5.0, 2);

   /* evaluate solution with zero tag */
   printf("evaluate expression with a zero tag\n");
   for( i = 1; i < 100; ++i )
   {
      SCIP_CALL( SCIPsetSolVal(scip, sol, x, i*i) );
      SCIP_CALL( SCIPsetSolVal(scip, sol, y, -5.0/i) );
      SCIP_CALL( SCIPevalConsExprExpr(scip, mainexpr, sol, 0) );
      checkExprEval(xexpr, yexpr, const_expr, prodexpr, sumexpr, mainexpr, i*i, -5.0 / i, 0);
   }

   /* mainexpr is not defined for x = -1 or y = 0; the result should be SCIP_INVALID */
   printf("evaluate expression for an undefined point\n");
   SCIP_CALL( SCIPsetSolVal(scip, sol, x, -1.0) );
   SCIP_CALL( SCIPsetSolVal(scip, sol, y, 0.0) );
   SCIP_CALL( SCIPevalConsExprExpr(scip, mainexpr, sol, 0) );
   cr_expect_eq(SCIPgetConsExprExprValue(mainexpr), SCIP_INVALID);

   /* set values for variable expression explicitly */
   printf("evaluate expression after setting value for variable expressions\n");
   for( i = 1; i < 3; ++i )
   {
      SCIPsetConsExprExprEvalValue(xexpr, i*i, i);
      SCIPsetConsExprExprEvalValue(yexpr, 1.0 / i, i);
      SCIP_CALL( SCIPevalConsExprExpr(scip, mainexpr, NULL, i) );
      checkExprEval(xexpr, yexpr, const_expr, prodexpr, sumexpr, mainexpr, i*i, 1.0 / i, i);
   }

   /* release all expressions */
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &xexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &yexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &const_expr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &prodexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &sumexpr) );
   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &mainexpr) );
}

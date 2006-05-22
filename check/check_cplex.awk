#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2006 Tobias Achterberg                              *
#*                                                                           *
#*                  2002-2006 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# $Id: check_cplex.awk,v 1.21 2006/05/22 11:01:28 bzfpfend Exp $
#
#@file    check_cplex.awk
#@brief   CPLEX Check Report Generator
#@author  Thorsten Koch
#@author  Tobias Achterberg
#
function abs(x)
{
    return x < 0 ? -x : x;
}
function max(x,y)
{
    return (x) > (y) ? (x) : (y);
}
BEGIN {
    printf("\\documentclass[leqno]{article}\n")                      >TEXFILE;
    printf("\\usepackage{a4wide}\n")                                 >TEXFILE;
    printf("\\usepackage{amsmath,amsfonts,amssymb,booktabs}\n")      >TEXFILE;
    printf("\\pagestyle{empty}\n\n")                                 >TEXFILE;
    printf("\\begin{document}\n\n")                                  >TEXFILE;
    printf("\\begin{table}[p]\n")                                    >TEXFILE;
    printf("\\begin{center}\n")                                      >TEXFILE;
    printf("\\setlength{\\tabcolsep}{2pt}\n")                        >TEXFILE;
    printf("\\newcommand{\\g}{\\raisebox{0.25ex}{\\tiny $>$}}\n")    >TEXFILE;
    printf("\\begin{tabular*}{\\textwidth}{@{\\extracolsep{\\fill}}lrrrrrrrrrrrr@{}}\n") >TEXFILE;
    printf("\\toprule\n")                                            >TEXFILE;
    printf("Name                &  Conss &   Vars &     Dual Bound &   Primal Bound &  Gap\\% &     Nodes &     Time \\\\\n") > TEXFILE;
    printf("\\midrule\n")                                            >TEXFILE;

    printf("-------------------------+-------+------+--------------+--------------+------+---------------+-------+------+--------------------+-------\n");
    printf("Name                     | Conss | Vars |   Dual Bound | Primal Bound | Gap% |               | Nodes | Time |                    |       \n");
    printf("-------------------------+-------+------+--------------+--------------+------+---------------+-------+------+--------------------+-------\n");

    timegeomshift = 0.0;
    nodegeomshift = 0.0;
    onlyinsolufile = 0;  # should only instances be reported that are included in the .solu file?

    nprobs   = 0;
    sbab     = 0;
    scut     = 0;
    stottime = 0.0;
    sgap     = 0.0;
    nodegeom = nodegeomshift;
    timegeom = timegeomshift;
    failtime = 0.0;
    timeouttime = 0.0;
    fail     = 0;
    pass     = 0;
    timeouts = 0;
}
/=opt=/  { solstatus[$2] = "opt"; sol[$2] = $3; }  # get optimum
/=inf=/  { solstatus[$2] = "inf"; sol[$2] = 0.0; } # problem infeasible
/=best=/ { solstatus[$2] = "best"; sol[$2] = $3; } # get best known solution value
/=unkn=/ { solstatus[$2] = "unkn"; }               # no feasible solution known
#
# problem name
#
/^@01/ { 
    n  = split ($2, a, "/");
    split(a[n], b, ".");
    prob = b[1];
    # Escape _ for TeX
    n = split(prob, a, "_");
    pprob = a[1];
    for( i = 2; i <= n; i++ )
       pprob = pprob "\\_" a[i];
    vars       = 0;
    cons       = 0;
    timeout    = 0;
    opti       = 0;
    feasible   = 1;
    cuts       = 0;
    pb         = 0.0;
    db         = 0.0;
    bbnodes    = 0;
    primlps    = 0;
    primiter   = 0;
    duallps    = 0;
    dualiter   = 0;
    sblps      = 0;
    sbiter     = 0;
    tottime    = 0.0;
}
#
# problem size
#
/^Reduced MIP has/ { cons = $4; vars = $6; }
#
# solution
#
/^Integer/  {
    if ($2 == "infeasible." || $2 == "infeasible")
    {
	db = 1e+20;
	pb = 1e+20;
	absgap = 0.0;
        feasible = 0;
    }
    else
    {
	db = $NF;
	pb = $NF;
	absgap = 0.0;
        feasible = 1;
    }
    opti = ($2 == "optimal") ? 1 : 0;
}
/^MIP - Integer/  { # since CPLEX 10.0
    if ($4 == "infeasible." || $4 == "infeasible")
    {
	db = 1e+20;
	pb = 1e+20;
	absgap = 0.0;
        feasible = 0;
    }
    else
    {
	db = $NF;
	pb = $NF;
	absgap = 0.0;
        feasible = 1;
    }
    opti = ($4 == "optimal") ? 1 : 0;
}
/^Solution limit exceeded, integer feasible:/ {
   pb = $8;
   timeout = 1;
}
/^MIP - Solution limit exceeded, integer feasible:/ { # since CPLEX 10.0
   pb = $10;
   timeout = 1;
}
/^Time/  {
    pb = ($4 == "no") ? 1e+75 : $8;
    timeout = 1;
}
/^MIP - Time/  { # since CPLEX 10.0
    pb = ($6 == "no") ? 1e+75 : $10;
    timeout = 1;
}
/^Node/ {
    pb = ($4 == "no") ? 1e+75 : $8;
    timeout = 1;
}
/^MIP - Node/ { # since CPLEX 10.0
    pb = ($6 == "no") ? 1e+75 : $10;
    timeout = 1;
}
/^Tree/  {
    pb = ($4 == "no") ? 1e+75 : $8;
    timeout = 1;
}
/^MIP - Tree/  { # since CPLEX 10.0
    pb = ($6 == "no") ? 1e+75 : $10;
    timeout = 1;
}
/^Error/  {
    pb = ($3 == "no") ? 1e+75 : $7;
}
/^MIP - Error/  { # since CPLEX 10.0
    pb = ($5 == "no") ? 1e+75 : $9;
}
/cuts applied/ { 
    cuts += $NF;
}
/^Current MIP best bound =/ {
    db = $6;
    split ($9, a, ",");
    absgap = a[1];
    if (opti == 1) 
	absgap = 0.0;
}
/^Solution/ {
   tottime   = $4;
   bbnodes   = $11;
}
/^=ready=/ {
   if( !onlyinsolufile || solstatus[prob] != "" )
   {
      bbnodes = max(bbnodes, 1); # CPLEX reports 0 nodes if the primal heuristics find the optimal solution in the root node

      nprobs++;
    
      optimal = 0;
      markersym = "\\g";
      if( abs(pb - db) < 1e-06 )
      {
         gap = 0.0;
         optimal = 1;
         markersym = "  ";
      }
      else if( abs(db) < 1e-06 )
         gap = -1.0;
      else if( pb*db < 0.0 )
         gap = -1.0;
      else if( abs(db) >= 1e+20 )
         gap = -1.0;
      else if( abs(pb) >= 1e+20 )
         gap = -1.0;
      else
         gap = 100.0*abs((pb-db)/db);
      if( gap < 0.0 )
         gapstr = "  --  ";
      else if( gap < 1e+04 )
         gapstr = sprintf("%6.1f", gap);
      else
         gapstr = " Large";

      printf("%-19s & %6d & %6d & %14.9g & %14.9g & %6s &%s%8d &%s%7.1f \\\\\n",
         pprob, cons, vars, db, pb, gapstr, markersym, bbnodes, markersym, tottime) >TEXFILE;
   
      printf("%-26s %6d %6d %14.9g %14.9g %6s                 %7d %6.1f                      ",
         prob, cons, vars, db, pb, gapstr, bbnodes, tottime);
   
      if( solstatus[prob] == "opt" )
      {
         if ((abs(pb - db) > 1e-4) || (abs(pb - sol[prob]) > 1e-6*max(abs(pb),1.0)))
         {
            if (timeout)
            {
               printf("timeout\n");
               timeouttime += tottime;
               timeouts++;
            }
            else
            {
               printf("fail\n");
               failtime += tottime;
               fail++;
            }
         }
         else
         {
            printf("ok\n");
            pass++;
         }
      }
      else if( solstatus[prob] == "inf" )
      {
         if (feasible)
         {
            if (timeout)
            {
               printf("timeout\n");
               timeouttime += tottime;
               timeouts++;
            }
            else
            {
               printf("fail\n");
               failtime += tottime;
               fail++;
            }
         }
         else
         {
            printf("ok\n");
            pass++;
         }
      }
      else
      {
         if (timeout)
         {
            printf("timeout\n");
            timeouttime += tottime;
            timeouts++;
         }
         else
            printf("unknown\n");
      }
   
      sbab     += bbnodes;
      scut     += cuts;
      stottime += tottime;
      timegeom = timegeom^((nprobs-1)/nprobs) * max(tottime+timegeomshift, 1.0)^(1.0/nprobs);
      nodegeom = nodegeom^((nprobs-1)/nprobs) * max(bbnodes+nodegeomshift, 1.0)^(1.0/nprobs);
   }
}
END {
   nodegeom -= nodegeomshift;
   timegeom -= timegeomshift;

   printf("\\midrule\n")                                                 >TEXFILE;
   printf("%-14s (%2d) &        &        &                &                &        & %9d & %8.1f \\\\\n",
      "Total", nprobs, sbab, stottime) >TEXFILE;
   printf("%-14s      &        &        &                &                &        & %9d & %8.1f \\\\\n",
      "Geom. Mean", nodegeom, timegeom) >TEXFILE;
   printf("\\bottomrule\n")                                              >TEXFILE;
   printf("\\noalign{\\vspace{6pt}}\n")                                  >TEXFILE;
   printf("\\end{tabular*}\n")                                           >TEXFILE;
   printf("\\caption{CPLEX with default settings}\n")                    >TEXFILE;
   printf("\\end{center}\n")                                             >TEXFILE;
   printf("\\end{table}\n")                                              >TEXFILE;
   printf("\\end{document}\n")                                           >TEXFILE;
    
   printf("-------------------------+-------+------+--------------+--------------+------+---------------+-------+------+--------------------+-------\n");
    
   printf("\n");
   printf("------------------------------[Nodes]---------------[Time]------\n");
   printf("  Cnt  Pass  Time  Fail  total(k)     geom.     total     geom. \n");
   printf("----------------------------------------------------------------\n");
   printf("%5d %5d %5d %5d %9d %9.1f %9.1f %9.1f\n",
      nprobs, pass, timeouts, fail, sbab / 1000, nodegeom, stottime, timegeom);
      printf("----------------------------------------------------------------\n");
}

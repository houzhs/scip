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
/*  SCIP is distributed under the terms of the SCIP Academic License.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: disp.c,v 1.42 2005/05/03 14:48:01 bzfpfend Exp $"

/**@file   disp.c
 * @brief  methods and datastructures for displaying runtime statistics
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "scip/def.h"
#include "scip/message.h"
#include "scip/memory.h"
#include "scip/set.h"
#include "scip/stat.h"
#include "scip/misc.h"
#include "scip/scip.h"
#include "scip/disp.h"

#include "scip/struct_disp.h"



/*
 * display column methods
 */

/** parameter change information method to autoselect display columns again */
DECL_PARAMCHGD(SCIPparamChgdDispActive)
{  /*lint --e{715}*/
   /* automatically select the now active display columns */
   CHECK_OKAY( SCIPautoselectDisps(scip) );

   return SCIP_OKAY;
}

/** creates a display column */
RETCODE SCIPdispCreate(
   DISP**           disp,               /**< pointer to store display column */
   SET*             set,                /**< global SCIP settings */
   BLKMEM*          blkmem,             /**< block memory for parameter settings */
   const char*      name,               /**< name of display column */
   const char*      desc,               /**< description of display column */
   const char*      header,             /**< head line of display column */
   DISPSTATUS       dispstatus,         /**< display activation status of display column */
   DECL_DISPFREE    ((*dispfree)),      /**< destructor of display column */
   DECL_DISPINIT    ((*dispinit)),      /**< initialize display column */
   DECL_DISPEXIT    ((*dispexit)),      /**< deinitialize display column */
   DECL_DISPINITSOL ((*dispinitsol)),   /**< solving process initialization method of display column */
   DECL_DISPEXITSOL ((*dispexitsol)),   /**< solving process deinitialization method of display column */
   DECL_DISPOUTPUT  ((*dispoutput)),    /**< output method */
   DISPDATA*        dispdata,           /**< display column data */
   int              width,              /**< width of display column (no. of chars used) */
   int              priority,           /**< priority of display column */
   int              position,           /**< relative position of display column */
   Bool             stripline           /**< should the column be separated with a line from its right neighbour? */
   )
{
   char paramname[MAXSTRLEN];
   char paramdesc[MAXSTRLEN];

   assert(disp != NULL);
   assert(name != NULL);
   assert(desc != NULL);
   assert(header != NULL);
   assert(dispoutput != NULL);
   assert(width >= 0);

   ALLOC_OKAY( allocMemory(disp) );
   ALLOC_OKAY( duplicateMemoryArray(&(*disp)->name, name, strlen(name)+1) );
   ALLOC_OKAY( duplicateMemoryArray(&(*disp)->desc, desc, strlen(desc)+1) );
   ALLOC_OKAY( duplicateMemoryArray(&(*disp)->header, header, strlen(header)+1) );
   (*disp)->dispstatus = dispstatus;
   (*disp)->dispfree = dispfree;
   (*disp)->dispinit = dispinit;
   (*disp)->dispexit = dispexit;
   (*disp)->dispinitsol = dispinitsol;
   (*disp)->dispexitsol = dispexitsol;
   (*disp)->dispoutput = dispoutput;
   (*disp)->dispdata = dispdata;
   (*disp)->width = width;
   (*disp)->priority = priority;
   (*disp)->position = position;
   (*disp)->stripline = stripline;
   (*disp)->initialized = FALSE;
   (*disp)->active = (dispstatus == SCIP_DISPSTATUS_ON);

   /* add parameters */
   sprintf(paramname, "display/%s/active", name);
   sprintf(paramdesc, "display activation status of display column <%s> (0: off, 1: auto, 2:on)", name);
   CHECK_OKAY( SCIPsetAddIntParam(set, blkmem, paramname, paramdesc,
                  (int*)(&(*disp)->dispstatus), (int)dispstatus, 0, 2, SCIPparamChgdDispActive, NULL) );

   return SCIP_OKAY;
}
   
/** frees memory of display column */
RETCODE SCIPdispFree(
   DISP**           disp,               /**< pointer to display column data structure */
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(disp != NULL);
   assert(*disp != NULL);
   assert(!(*disp)->initialized);
   assert(set != NULL);

   /* call destructor of display column */
   if( (*disp)->dispfree != NULL )
   {
      CHECK_OKAY( (*disp)->dispfree(set->scip, *disp) );
   }

   freeMemoryArray(&(*disp)->name);
   freeMemoryArray(&(*disp)->desc);
   freeMemoryArray(&(*disp)->header);
   freeMemory(disp);

   return SCIP_OKAY;
}

/** initializes display column */
RETCODE SCIPdispInit(
   DISP*            disp,               /**< display column */
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(disp != NULL);
   assert(set != NULL);

   if( disp->initialized )
   {
      errorMessage("display column <%s> already initialized\n", disp->name);
      return SCIP_INVALIDCALL;
   }

   if( disp->dispinit != NULL )
   {
      CHECK_OKAY( disp->dispinit(set->scip, disp) );
   }
   disp->initialized = TRUE;

   return SCIP_OKAY;
}

/** deinitializes display column */
RETCODE SCIPdispExit(
   DISP*            disp,               /**< display column */
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(disp != NULL);
   assert(set != NULL);

   if( !disp->initialized )
   {
      errorMessage("display column <%s> not initialized\n", disp->name);
      return SCIP_INVALIDCALL;
   }

   if( disp->dispexit != NULL )
   {
      CHECK_OKAY( disp->dispexit(set->scip, disp) );
   }
   disp->initialized = FALSE;

   return SCIP_OKAY;
}

/** informs display column that the branch and bound process is being started */
RETCODE SCIPdispInitsol(
   DISP*            disp,               /**< display column */
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(disp != NULL);
   assert(set != NULL);

   /* call solving process initialization method of display column */
   if( disp->dispinitsol != NULL )
   {
      CHECK_OKAY( disp->dispinitsol(set->scip, disp) );
   }

   return SCIP_OKAY;
}

/** informs display column that the branch and bound process data is being freed */
RETCODE SCIPdispExitsol(
   DISP*            disp,               /**< display column */
   SET*             set                 /**< global SCIP settings */
   )
{
   assert(disp != NULL);
   assert(set != NULL);

   /* call solving process deinitialization method of display column */
   if( disp->dispexitsol != NULL )
   {
      CHECK_OKAY( disp->dispexitsol(set->scip, disp) );
   }

   return SCIP_OKAY;
}

/** output display column to screen */
RETCODE SCIPdispOutput(
   DISP*            disp,               /**< display column */
   SET*             set,                /**< global SCIP settings */
   FILE*            file                /**< output file (or NULL for standard output) */
   )
{
   assert(disp != NULL);
   assert(disp->dispoutput != NULL);
   assert(set != NULL);

   if( file == NULL )
      file = stdout;

   CHECK_OKAY( disp->dispoutput(set->scip, disp, file) );

   return SCIP_OKAY;
}

/** gets user data of display column */
DISPDATA* SCIPdispGetData(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->dispdata;
}

/** sets user data of display column; user has to free old data in advance! */
void SCIPdispSetData(
   DISP*            disp,               /**< display column */
   DISPDATA*        dispdata            /**< new display column user data */
   )
{
   assert(disp != NULL);

   disp->dispdata = dispdata;
}

/** gets name of display column */
const char* SCIPdispGetName(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->name;
}

/** gets description of display column */
const char* SCIPdispGetDesc(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->desc;
}

/** gets head line of display column */
const char* SCIPdispGetHeader(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->header;
}

/** gets width of display column */
int SCIPdispGetWidth(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->width;
}

/** gets priority of display column */
int SCIPdispGetPriority(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->priority;
}

/** gets position of display column */
int SCIPdispGetPosition(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->position;
}

/** gets status of display column */
DISPSTATUS SCIPdispGetStatus(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->dispstatus;
}

/** is display column initialized? */
Bool SCIPdispIsInitialized(
   DISP*            disp                /**< display column */
   )
{
   assert(disp != NULL);

   return disp->initialized;
}

/** prints one line of output with the active display columns */
RETCODE SCIPdispPrintLine(
   SET*             set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics data */
   FILE*            file,               /**< output file (or NULL for standard output) */
   Bool             forcedisplay        /**< should the line be printed without regarding frequency? */
   )
{
   assert(set != NULL);
   assert(set->disp_freq >= -1);
   assert(set->disp_headerfreq >= -1);
   assert(stat != NULL);

   if( (VERBLEVEL)set->disp_verblevel < SCIP_VERBLEVEL_NORMAL || set->disp_freq == -1 )
      return SCIP_OKAY;

   if( forcedisplay
      || (stat->nnodes != stat->lastdispnode
         && set->disp_freq > 0
         && (stat->nnodes % set->disp_freq == 0 || stat->nnodes == 1)) )
   {
      int i;
      int j;
      Bool stripline;

      /* use stdout, if file is unspecified */
      if( file == NULL )
         file = stdout;

      /* display header line */
      if( (set->disp_headerfreq == 0 && stat->ndisplines == 0)
         || (set->disp_headerfreq > 0 && stat->ndisplines % set->disp_headerfreq == 0) )
      {
         int fillspace;

         stripline = FALSE;
         for( i = 0; i < set->ndisps; ++i )
         {
            assert(set->disps[i] != NULL);
            if( set->disps[i]->active )
            {
               if( stripline )
                  fprintf(file, "|");
               fillspace = set->disps[i]->width - (int)strlen(set->disps[i]->header);
               for( j = 0; j < (fillspace)/2; ++j )
                  fprintf(file, " ");
               fprintf(file, set->disps[i]->header);
               for( j = 0; j < (fillspace+1)/2; ++j )
                  fprintf(file, " ");
               stripline = set->disps[i]->stripline;
            }
         }
         fprintf(file, "\n");
      }

      /* display node information line */
      stripline = FALSE;
      for( i = 0; i < set->ndisps; ++i )
      {
         assert(set->disps[i] != NULL);
         if( set->disps[i]->active )
         {
            if( stripline )
               fprintf(file, "|");
            CHECK_OKAY( SCIPdispOutput(set->disps[i], set, file) );
            stripline = set->disps[i]->stripline;
         }
      }
      fprintf(file, "\n");
      fflush(stdout);

      stat->lastdispnode = stat->nnodes;
      stat->ndisplines++;
   }

   return SCIP_OKAY;
}

/** comparison method for display colums */
static
DECL_SORTPTRCOMP(dispComp)
{  /*lint --e{715}*/
   return ((DISP*)elem2)->priority - ((DISP*)elem1)->priority;
}

/** activates all display lines fitting in the display w.r. to priority */
RETCODE SCIPdispAutoActivate(
   SET*             set                 /**< global SCIP settings */
   )
{
   DISP** disps;
   int totalwidth;
   int width;
   int i;

   assert(set != NULL);

   /* sort display columns w.r. to their priority */
   ALLOC_OKAY( duplicateMemoryArray(&disps, set->disps, set->ndisps) );
   SCIPbsortPtr((void**)disps, set->ndisps, dispComp);

   totalwidth = 0;

   /* first activate all columns with display status ON */
   for( i = 0; i < set->ndisps; ++i )
   {
      width = disps[i]->width;
      if( disps[i]->stripline )
         width++;
      if( disps[i]->dispstatus == SCIP_DISPSTATUS_ON )
      {
         disps[i]->active = TRUE;
         totalwidth += width;
      }
      else
         disps[i]->active = FALSE;
   }

   /* beginning with highest priority display column, activate AUTO columns as long as it fits into display width */
   for( i = 0; i < set->ndisps; ++i )
   {
      if( disps[i]->dispstatus == SCIP_DISPSTATUS_AUTO )
      {
         assert(!disps[i]->active);

         width = disps[i]->width;
         if( disps[i]->stripline )
            width++;
         if( totalwidth + width <= set->disp_width )
         {
            disps[i]->active = TRUE;
            totalwidth += width;
         }
      }
   }

   /* free temporary memory */
   freeMemoryArray(&disps);

   return SCIP_OKAY;
}

static
const char decpowerchar[] = {' ', 'k', 'M', 'G', 'T', 'P', 'E'};
#define MAXDECPOWER 6

/** displays a long integer in decimal form fitting in a given width */
void SCIPdispLongint(
   FILE*            file,               /**< output stream */
   Longint          val,                /**< value to display */
   int              width               /**< width to fit into */
   )
{
   assert(width >= 1);

   if( width == 1 )
   {
      if( val < 0 )
         fprintf(file, "-");
      else if( val < 10 )
         fprintf(file, "%lld", val);
      else
         fprintf(file, "+");
   }
   else
   {
      char format[MAXSTRLEN];
      Longint maxval;
      int decpower;
      int i;

      maxval = 1;
      for( i = 0; i < width-1; ++i )
         maxval *= 10;
      if( val < 0 )
         maxval /= 10;
      decpower = 0;
      while( ABS(val) >= maxval && decpower < MAXDECPOWER )
      {
         decpower++;
         val /= 1000;
      }
      sprintf(format, "%%%dlld%c", width-1, decpowerchar[decpower]);

      if( width == 2 && val < 0 )
         fprintf(file, "-%c", decpowerchar[decpower]);
      else
         fprintf(file, format, val);
   }
}

/** displays an integer in decimal form fitting in a given width */
void SCIPdispInt(
   FILE*            file,               /**< output stream */
   int              val,                /**< value to display */
   int              width               /**< width to fit into */
   )
{
   SCIPdispLongint(file, (Longint)val, width);
}


static
const char timepowerchar[] = {'s', 'm', 'h', 'd', 'y'};
const Real timepowerval[] = {1.0, 60.0, 60.0, 24.0, 365.0};
#define MAXTIMEPOWER 4

/** displays a time value fitting in a given width */
void SCIPdispTime(
   FILE*            file,               /**< output stream */
   Real             val,                /**< value in seconds to display */
   int              width               /**< width to fit into */
   )
{
   assert(width >= 1);

   if( width == 1 )
   {
      if( val < 0.0 )
         fprintf(file, "-");
      else if( val < 10.0 )
         fprintf(file, "%.0f", val);
      else
         fprintf(file, "+");
   }
   else
   {
      char format[MAXSTRLEN];
      Longint maxval;
      int timepower;
      int i;

      maxval = 1;
      for( i = 0; i < width-1; ++i )
         maxval *= 10;
      if( val < 0.0 )
         maxval /= 10;
      timepower = 0;
      while( REALABS(val) + 0.5 >= maxval && timepower < MAXTIMEPOWER )
      {
         timepower++;
         val /= timepowerval[timepower];
      }
      if( REALABS(val) + 0.05 < maxval/100 ) /*lint !e653*/
         sprintf(format, "%%%d.1f%c", width-1, timepowerchar[timepower]);
      else
         sprintf(format, "%%%d.0f%c", width-1, timepowerchar[timepower]);

      if( width == 2 && val < 0.0 )
         fprintf(file, "-%c", timepowerchar[timepower]);
      else
         fprintf(file, format, val);
   }
}

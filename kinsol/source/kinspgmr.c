/*
 * -----------------------------------------------------------------
 * $Revision: 1.26.2.1 $
 * $Date: 2005/04/07 00:25:27 $
 * -----------------------------------------------------------------
 * Programmer(s): Allan Taylor, Alan Hindmarsh, Radu Serban, and
 *                Aaron Collier @ LLNL
 * -----------------------------------------------------------------
 * Copyright (c) 2002, The Regents of the University of California.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see sundials/kinsol/LICENSE.
 * -----------------------------------------------------------------
 * This is the implementation file for the KINSOL scaled,
 * preconditioned GMRES linear solver, KINSpgmr.
 * -----------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "kinsol_impl.h"
#include "kinspgmr_impl.h"
#include "sundialsmath.h"

/*
 * -----------------------------------------------------------------
 * private constants
 * -----------------------------------------------------------------
 */

#define ZERO RCONST(0.0)
#define ONE  RCONST(1.0)
#define TWO  RCONST(2.0)

/*
 * -----------------------------------------------------------------
 * keys for KINSpgmrPrintInfo
 * -----------------------------------------------------------------
 */

#define PRNT_NLI   1
#define PRNT_EPS   2

/*
 * -----------------------------------------------------------------
 * function prototypes
 * -----------------------------------------------------------------
 */

/* KINSpgmr linit, lsetup, lsolve, and lfree routines */

static int KINSpgmrInit(KINMem kin_mem);
static int KINSpgmrSetup(KINMem kin_mem);
static int KINSpgmrSolve(KINMem kin_mem, N_Vector xx, N_Vector bb,
                         realtype *res_norm);
static int KINSpgmrFree(KINMem kin_mem);

/* KINSpgmr Atimes and PSolve routines called by generic SPGMR solver */

static int KINSpgmrAtimes(void *kinsol_mem, N_Vector v, N_Vector z);
static int KINSpgmrPSolve(void *kinsol_mem, N_Vector r, N_Vector z, int lr);

/* difference quotient approximation for jacobian times vector */

static int KINSpgmrDQJtimes(N_Vector v, N_Vector Jv,
                            N_Vector u, booleantype *new_u, 
                            void *jac_data);

static void KINSpgmrPrintInfo(KINMem kin_mem, char *funcname, int key,...);

/*
 * -----------------------------------------------------------------
 * readability replacements
 * -----------------------------------------------------------------
 */

#define lrw1           (kin_mem->kin_lrw1)
#define liw1           (kin_mem->kin_liw1)
#define uround         (kin_mem->kin_uround)
#define nfe            (kin_mem->kin_nfe)
#define nni            (kin_mem->kin_nni)
#define nnilpre        (kin_mem->kin_nnilpre)
#define func           (kin_mem->kin_func)
#define f_data         (kin_mem->kin_f_data)
#define printfl        (kin_mem->kin_printfl)
#define linit          (kin_mem->kin_linit)
#define lsetup         (kin_mem->kin_lsetup)
#define lsolve         (kin_mem->kin_lsolve)
#define lfree          (kin_mem->kin_lfree)
#define lmem           (kin_mem->kin_lmem)
#define uu             (kin_mem->kin_uu)
#define fval           (kin_mem->kin_fval)
#define uscale         (kin_mem->kin_uscale)
#define fscale         (kin_mem->kin_fscale)
#define sqrt_relfunc   (kin_mem->kin_sqrt_relfunc)
#define precondcurrent (kin_mem->kin_precondcurrent)
#define eps            (kin_mem->kin_eps)
#define sJpnorm        (kin_mem->kin_sJpnorm)
#define sfdotJp        (kin_mem->kin_sfdotJp)
#define errfp          (kin_mem->kin_errfp)
#define infofp         (kin_mem->kin_infofp)
#define setupNonNull   (kin_mem->kin_setupNonNull)
#define vtemp1         (kin_mem->kin_vtemp1)
#define vec_tmpl       (kin_mem->kin_vtemp1)
#define vtemp2         (kin_mem->kin_vtemp2)

#define pretype   (kinspgmr_mem->g_pretype)
#define gstype    (kinspgmr_mem->g_gstype)
#define nli       (kinspgmr_mem->g_nli)
#define npe       (kinspgmr_mem->g_npe)
#define nps       (kinspgmr_mem->g_nps)
#define ncfl      (kinspgmr_mem->g_ncfl)
#define njtimes   (kinspgmr_mem->g_njtimes)
#define nfeSG     (kinspgmr_mem->g_nfeSG)
#define new_uu    (kinspgmr_mem->g_new_uu)
#define spgmr_mem (kinspgmr_mem->g_spgmr_mem)
#define last_flag (kinspgmr_mem->g_last_flag)

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmr
 * -----------------------------------------------------------------
 * This routine allocates and initializes the memory record and
 * sets function fields specific to the SPGMR linear solver module.
 * KINSpgmr sets the kin_linit, kin_lsetup, kin_lsolve, and
 * kin_lfree fields in *kinmem to be KINSpgmrInit, KINSpgmrSetup,
 * KINSpgmrSolve, and KINSpgmrFree, respectively. It allocates
 * memory for a structure of type KINSpgmrMemRec and sets the
 * kin_lmem field in *kinmem to the address of this structure. It
 * also calls SpgmrMalloc to allocate memory for the module
 * SPGMR. In summary, KINSpgmr sets the following fields in the
 * KINSpgmrMemRec structure:
 *
 *  g_pretype   = PREC_NONE
 *  g_gstype    = MODIFIED_GS
 *  g_maxl      = KINSPGMR_MAXL  if maxl <= 0
 *              = maxl           if maxl > 0
 *  g_maxlrst   = 0 (default)
 *  g_last_flag = KINSPGMR_SUCCESS
 *  g_pset      = NULL
 *  g_psolve    = NULL
 *  g_P_data    = NULL
 *  g_jtimes    = NULL
 *  g_J_data    = NULL
 * -----------------------------------------------------------------
 */

int KINSpgmr(void *kinmem, int maxl)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;
  int maxl1;

  if (kinmem == NULL){
    fprintf(stderr, MSGS_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);  
  }
  kin_mem = (KINMem) kinmem;

  /* check for required vector operations */

  /* Note: do NOT need to check for N_VClone, N_VDestory, N_VLinearSum, N_VProd,
     N_VScale, N_VDiv, or N_VWL2Norm because they are required by KINSOL */

  if ((vec_tmpl->ops->nvconst == NULL) ||
      (vec_tmpl->ops->nvdotprod == NULL) ||
      (vec_tmpl->ops->nvl1norm == NULL)) {
    if (errfp != NULL) fprintf(errfp, MSGS_BAD_NVECTOR);
    return(KINSPGMR_ILL_INPUT);
  }

  /* set four main function fields in kin_mem */

  linit  = KINSpgmrInit; 
  lsetup = KINSpgmrSetup;
  lsolve = KINSpgmrSolve;
  lfree  = KINSpgmrFree;

  /* get memory for KINSpgmrMemRec */

  kinspgmr_mem = (KINSpgmrMem) malloc(sizeof(KINSpgmrMemRec));
  if (kinspgmr_mem == NULL){
    fprintf(errfp, MSGS_MEM_FAIL);
    return(KINSPGMR_MEM_FAIL);  
  }

  /* set SPGMR parameters that were passed in call sequence */

  maxl1 = (maxl <= 0) ? KINSPGMR_MAXL : maxl;
  kinspgmr_mem->g_maxl = maxl1;  

  /* set default values for the rest of the SPGMR parameters */

  kinspgmr_mem->g_pretype   = PREC_NONE;
  kinspgmr_mem->g_gstype    = MODIFIED_GS;
  kinspgmr_mem->g_maxlrst   = 0;
  kinspgmr_mem->g_last_flag = KINSPGMR_SUCCESS;
  kinspgmr_mem->g_pset      = NULL;
  kinspgmr_mem->g_psolve    = NULL;
  kinspgmr_mem->g_P_data    = NULL;
  kinspgmr_mem->g_jtimes    = NULL;
  kinspgmr_mem->g_J_data    = NULL;

  /* call SpgmrMalloc to allocate workspace for SPGMR */

  /* vec_tmpl passed as template vector */

  spgmr_mem = SpgmrMalloc(maxl1, vec_tmpl);
  if (spgmr_mem == NULL) {
    fprintf(errfp, MSGS_MEM_FAIL);
    lmem = NULL;  /* set lmem to NULL and free that memory as a flag to a
                     later inadvertent KINSol call that SpgmrMalloc failed */
    free(lmem);
    return(KINSPGMR_MEM_FAIL);
  }

  /* attach linear solver memory to KINSOL memory */

  lmem = kinspgmr_mem;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrSetMaxRestarts
 * -----------------------------------------------------------------
 */

int KINSpgmrSetMaxRestarts(void *kinmem, int maxrs)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;

  /* check for legal maxrs */

  if (maxrs < 0) {
    fprintf(errfp, MSGS_KINS_NEG_MAXRS);
    return(KINSPGMR_ILL_INPUT);
  }
  kinspgmr_mem->g_maxlrst = maxrs;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrSetPreconditioner
 * -----------------------------------------------------------------
 */

int KINSpgmrSetPreconditioner(void *kinmem,
			      KINSpgmrPrecSetupFn pset,
			      KINSpgmrPrecSolveFn psolve,
			      void *P_data)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;

  kinspgmr_mem->g_pset   = pset;
  kinspgmr_mem->g_psolve = psolve;
  kinspgmr_mem->g_P_data = P_data;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrSetJacTimesVecFn
 * -----------------------------------------------------------------
 */

int KINSpgmrSetJacTimesVecFn(void *kinmem,
			     KINSpgmrJacTimesVecFn jtimes,
			     void *J_data)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;

  kinspgmr_mem->g_jtimes = jtimes;
  kinspgmr_mem->g_J_data = J_data;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetWorkSpace
 * -----------------------------------------------------------------
 */

int KINSpgmrGetWorkSpace(void *kinmem, long int *lenrwSG, long int *leniwSG)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;
  int maxl;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;

  maxl = kinspgmr_mem->g_maxl;

  *lenrwSG = lrw1 * (maxl + 3) + (maxl * (maxl + 4)) + 1;
  *leniwSG = liw1 * (maxl + 3);

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetNumPrecEvals
 * -----------------------------------------------------------------
 */

int KINSpgmrGetNumPrecEvals(void *kinmem, long int *npevals)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;
  *npevals = npe;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetNumPrecSolves
 * -----------------------------------------------------------------
 */

int KINSpgmrGetNumPrecSolves(void *kinmem, long int *npsolves)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;
  *npsolves = nps;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetNumLinIters
 * -----------------------------------------------------------------
 */

int KINSpgmrGetNumLinIters(void *kinmem, long int *nliters)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;
  *nliters = nli;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetNumConvFails
 * -----------------------------------------------------------------
 */

int KINSpgmrGetNumConvFails(void *kinmem, long int *nlcfails)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;
  *nlcfails = ncfl;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetNumJtimesEvals
 * -----------------------------------------------------------------
 */

int KINSpgmrGetNumJtimesEvals(void *kinmem, long int *njvevals)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;
  *njvevals = njtimes;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetNumFuncEvals
 * -----------------------------------------------------------------
 */

int KINSpgmrGetNumFuncEvals(void *kinmem, long int *nfevalsSG)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(errfp, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;
  *nfevalsSG = nfeSG;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrGetLastFlag
 * -----------------------------------------------------------------
 */

int KINSpgmrGetLastFlag(void *kinmem, int *flag)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* return immediately if kinmem is NULL */

  if (kinmem == NULL) {
    fprintf(stderr, MSGS_SETGET_KINMEM_NULL);
    return(KINSPGMR_MEM_NULL);
  }
  kin_mem = (KINMem) kinmem;

  if (lmem == NULL) {
    fprintf(stderr, MSGS_SETGET_LMEM_NULL);
    return(KINSPGMR_LMEM_NULL);
  }
  kinspgmr_mem = (KINSpgmrMem) lmem;

  *flag = last_flag;

  return(KINSPGMR_SUCCESS);
}

/*
 * -----------------------------------------------------------------
 * additional readability replacements
 * -----------------------------------------------------------------
 */

#define maxl    (kinspgmr_mem->g_maxl)
#define maxlrst (kinspgmr_mem->g_maxlrst)
#define pset    (kinspgmr_mem->g_pset)
#define psolve  (kinspgmr_mem->g_psolve)
#define P_data  (kinspgmr_mem->g_P_data)
#define jtimes  (kinspgmr_mem->g_jtimes)
#define J_data  (kinspgmr_mem->g_J_data)

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrInit
 * -----------------------------------------------------------------
 * This routine initializes variables associated with the GMRES
 * linear solver. Memory allocation was done previously in
 * KINSpgmr.
 * -----------------------------------------------------------------
 */

static int KINSpgmrInit(KINMem kin_mem)
{
  KINSpgmrMem kinspgmr_mem;

  kinspgmr_mem = (KINSpgmrMem) lmem;

  /* initialize counters */

  npe = nli = nps = ncfl = 0;
  njtimes = nfeSG = 0;

  /* set preconditioner type */

  if (psolve != NULL) {
    pretype = PREC_RIGHT;
  } else {
    pretype = PREC_NONE;
  }
  
  /* set setupNonNull to TRUE iff there is preconditioning with setup */

  setupNonNull = (psolve != NULL) && (pset != NULL);

  /* if jtimes is NULL at this time, set it to private DQ routine */

  if (jtimes == NULL) {
    jtimes = KINSpgmrDQJtimes;
    J_data = kin_mem;
  }

  last_flag = KINSPGMR_SUCCESS;
  return(0);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrSetup
 * -----------------------------------------------------------------
 * This routine does the setup operations for the SPGMR linear
 * solver, that is, it is an interface to the user-supplied
 * routine pset.
 * -----------------------------------------------------------------
 */

static int KINSpgmrSetup(KINMem kin_mem)
{
  KINSpgmrMem kinspgmr_mem;
  int ret;

  kinspgmr_mem = (KINSpgmrMem) lmem;

  /* call pset routine */

  ret = pset(uu, uscale, fval, fscale, P_data, vtemp1, vtemp2); 

  last_flag = ret;

  if (ret != 0) return(1);

  npe++;
  nnilpre = nni; 

  /* return the same value ret that pset returned */

  return(0);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrSolve
 * -----------------------------------------------------------------
 * This routine handles the call to the generic SPGMR solver
 * SpgmrSolve for the solution of the linear system Ax = b.
 *
 * Appropriate variables are passed to SpgmrSolve and the counters
 * nli, nps, and ncfl are incremented, and the return value is set
 * according to the success of SpgmrSolve. The success flag is
 * returned if SpgmrSolve converged, or if the residual was reduced.
 * Of the other error conditions, only preconditioner solver
 * failure is specifically returned. Otherwise a generic flag is
 * returned to denote failure of this routine.
 * -----------------------------------------------------------------
 */

static int KINSpgmrSolve(KINMem kin_mem, N_Vector xx, N_Vector bb, 
                         realtype *res_norm)
{
  KINSpgmrMem kinspgmr_mem;
  int ret, nli_inc, nps_inc;
  
  kinspgmr_mem = (KINSpgmrMem) lmem;

  /* Set initial guess to xx = 0. bb is set, by the routine
     calling KINSpgmrSolve, to the RHS vector for the system
     to be solved. */ 
 
  N_VConst(ZERO, xx);

  new_uu = TRUE;  /* set flag required for user Jacobian routine */

  /* call SpgmrSolve */

  ret = SpgmrSolve(spgmr_mem, kin_mem, xx, bb, pretype, gstype, eps, 
                   maxlrst, kin_mem, fscale, fscale, KINSpgmrAtimes,
                   KINSpgmrPSolve, res_norm, &nli_inc, &nps_inc);

  /* increment counters nli, nps, and ncfl 
     (nni is updated in the KINSol main iteration loop) */

  nli = nli + (long int) nli_inc;
  nps = nps + (long int) nps_inc;

  if (printfl > 2) 
    KINSpgmrPrintInfo(kin_mem, "KINSpgmrSolve", PRNT_NLI, &nli_inc);

  if (ret != 0) ncfl++;

  /* Compute the terms sJpnorm and sfdotJp for use in the global strategy
     routines and in KINForcingTerm. Both of these terms are subsequently
     corrected if the step is reduced by constraints or the line search.

     sJpnorm is the norm of the scaled product (scaled by fscale) of
     the current Jacobian matrix J and the step vector p.

     sfdotJp is the dot product of the scaled f vector and the scaled
     vector J*p, where the scaling uses fscale. */

  KINSpgmrAtimes(kin_mem, xx, bb);
  sJpnorm = N_VWL2Norm(bb,fscale);
  N_VProd(bb, fscale, bb);
  N_VProd(bb, fscale, bb);
  sfdotJp = N_VDotProd(fval, bb);

  if (printfl > 2)
    KINSpgmrPrintInfo(kin_mem, "KINSpgmrSolve", PRNT_EPS, res_norm, &eps);

  /* set return value to appropriate value */

  last_flag = ret;

  if ((ret == SPGMR_SUCCESS) || (ret == SPGMR_RES_REDUCED)) return(0);
  else if (ret == SPGMR_PSOLVE_FAIL_REC) return(1);
  else return(-1);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrFree
 * -----------------------------------------------------------------
 * This routine frees memory specific to the SPGMR linear solver.
 * -----------------------------------------------------------------
 */

static int KINSpgmrFree(KINMem kin_mem)
{
  KINSpgmrMem kinspgmr_mem;

  kinspgmr_mem = (KINSpgmrMem) lmem;

  SpgmrFree(spgmr_mem);
  free(lmem);

  return(0);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrAtimes
 * -----------------------------------------------------------------
 * This routine coordinates the generation of the matrix-vector
 * product z = J*v by calling either KINSpgmrDQJtimes, which uses
 * a difference quotient approximation for J*v, or by calling the
 * user-supplied routine KINSpgmrJacTimesVecFn if it is non-null.
 * -----------------------------------------------------------------
 */

static int KINSpgmrAtimes(void *kinsol_mem, N_Vector v, N_Vector z)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;
  int ret;

  kin_mem = (KINMem) kinsol_mem;
  kinspgmr_mem = (KINSpgmrMem) lmem;

  ret = jtimes(v, z, uu, &new_uu, J_data);
  njtimes++;

  return(ret);
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrPSolve
 * -----------------------------------------------------------------
 * This routine interfaces between the generic SpgmrSolve routine
 * and the user's psolve routine. It passes to psolve all required
 * state information from kinsol_mem. Its return value is the same
 * as that returned by psolve. Note that the generic SPGMR solver
 * guarantees that KINSpgmrPSolve will not be called in the case in
 * which preconditioning is not done. This is the only case in which
 * the user's psolve routine is allowed to be NULL.
 * -----------------------------------------------------------------
 */

static int KINSpgmrPSolve(void *kinsol_mem, N_Vector r, N_Vector z, int lrdummy)
{
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;
  int ret;

  kin_mem = (KINMem) kinsol_mem;
  kinspgmr_mem = (KINSpgmrMem) lmem;

  /* copy the rhs into z before the psolve call */   
  /* Note: z returns with the solution */

  N_VScale(ONE, r, z);

  /* this call is counted in nps within the KINSpgmrSolve routine */

  ret = psolve(uu, uscale, fval, fscale, z, P_data, vtemp1);

  return(ret);     
}

/*
 * -----------------------------------------------------------------
 * Function : KINSpgmrDQJtimes
 * -----------------------------------------------------------------
 * This routine generates the matrix-vector product z = J*v using a
 * difference quotient approximation. The approximation is 
 * J*v = [func(uu + sigma*v) - func(uu)]/sigma. Here sigma is based
 * on the dot products (uscale*uu, uscale*v) and
 * (uscale*v, uscale*v), the L1Norm(uscale*v), and on sqrt_relfunc
 * (the square root of the relative error in the function). Note
 * that v in the argument list has already been both preconditioned
 * and unscaled.
 * -----------------------------------------------------------------
 */

static int KINSpgmrDQJtimes(N_Vector v, N_Vector Jv,
                            N_Vector u, booleantype *new_u, 
                            void *jac_data)
{
  realtype sigma, sigma_inv, sutsv, sq1norm, sign, vtv;
  KINMem kin_mem;
  KINSpgmrMem kinspgmr_mem;

  /* jac_data is kin_mem */

  kin_mem = (KINMem) jac_data;
  kinspgmr_mem = (KINSpgmrMem) lmem;

  /* scale the vector v and put Du*v into vtemp1 */

  N_VProd(v, uscale, vtemp1);

  /* scale u and put into Jv (used as a temporary storage) */

  N_VProd(u, uscale, Jv);

  /* compute dot product (Du*u).(Du*v) */

  sutsv = N_VDotProd(Jv, vtemp1);

  /* compute dot product (Du*v).(Du*v) */

  vtv = N_VDotProd(vtemp1, vtemp1);

  sq1norm = N_VL1Norm(vtemp1);

  sign = (sutsv >= ZERO) ? ONE : -ONE ;
 
  /*  this expression for sigma is from p. 469, Brown and Saad paper */

  sigma = sign*sqrt_relfunc*MAX(ABS(sutsv),sq1norm)/vtv; 

  sigma_inv = ONE/sigma;

  /* compute the u-prime at which to evaluate the function func */

  N_VLinearSum(ONE, u, sigma, v, vtemp1);
 
  /* call the system function to calculate func(u+sigma*v) */

  func(vtemp1, vtemp2, f_data);    
  nfeSG++;

  /* finish the computation of the difference quotient */

  N_VLinearSum(sigma_inv, vtemp2, -sigma_inv, fval, Jv);

  return(0);
}


/*
 * -----------------------------------------------------------------
 * KINSpgmrPrintInfo
 * -----------------------------------------------------------------
 */

static void KINSpgmrPrintInfo(KINMem kin_mem, char *funcname, int key,...)
{
  va_list ap;
  realtype rnum1, rnum2;
  int inum1;

  fprintf(infofp, "---%s\n   ", funcname);

  /* initialize argument processing */

  va_start(ap, key); 

  switch(key) {

  case PRNT_NLI:
    inum1 = *(va_arg(ap, int *));
    fprintf(infofp, "nli_inc = %d\n", inum1);
    break;
    
  case PRNT_EPS:
    rnum1 = *(va_arg(ap, realtype *));
    rnum2 = *(va_arg(ap, realtype *));
#if defined(SUNDIALS_EXTENDED_PRECISION)
    fprintf(infofp, "residual norm = %12.3Lg  eps = %12.3Lg\n", rnum1, rnum2);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
    fprintf(infofp, "residual norm = %12.3lg  eps = %12.3lg\n", rnum1, rnum2);
#else
    fprintf(infofp, "residual norm = %12.3g  eps = %12.3g\n", rnum1, rnum2);
#endif
      break;

  }

  /* finalize argument processing */

  va_end(ap);

  return;
}

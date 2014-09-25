/*****************************************************************************
 *
 *  colloids_Q_tensor.c
 *
 *  Routines dealing with LC anchoring at surfaces.
 *
 *  $Id$
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Juho Lintuvuori (jlintuvu@ph.ed.ac.uk)
 *  (c) 2011 The University of Edinburgh
 *  
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "pe.h"
#include "build.h"
#include "coords.h"
#include "colloids.h"
#include "io_harness.h"
#include "util.h"
#include "model.h"
#include "blue_phase.h"
#include "colloids_Q_tensor.h"

static int anchoring_coll_ = ANCHORING_NORMAL;
static int anchoring_wall_ = ANCHORING_NORMAL;

/*
 * Normal anchoring free energy
 * f_s = (1/2) w_1 ( Q_ab - Q^0_ab )^2  with Q^0_ab prefered orientation.
 *
 * Planar anchoring free energy (Fournier and Galatola EPL (2005).
 * f_s = (1/2) w_1 ( Q^tilde_ab - Q^tidle_perp_ab )^2
 *     + (1/2) w_2 ( Q^tidle^2 - S_0^2 )^2
 *
 * so w_2 must be zero for normal anchoring.
 */

static double w1_colloid_ = 0.0;
static double w2_colloid_ = 0.0;
static double w1_wall_ = 0.0;
static double w2_wall_ = 0.0;

static colloids_info_t * cinfo_ = NULL; /* Temporary solution to getting map */

/*****************************************************************************
 *
 *  colloids_q_cinfo_set
 *
 *****************************************************************************/

int colloids_q_cinfo_set(colloids_info_t * cinfo) {

  assert(cinfo);

  cinfo_ = cinfo;
  return 0;
}

/*****************************************************************************
 *
 *  colloids_q_boundary_normal
 *
 *  Find the 'true' outward unit normal at fluid site index. Note that
 *  the half-way point is not used to provide a simple unique value in
 *  the gradient calculation.
 *
 *  The unit lattice vector which is the discrete outward normal is di[3].
 *  The result is returned in unit vector dn.
 *
 *****************************************************************************/

void colloids_q_boundary_normal(const int index, const int di[3],
				double dn[3]) {
  int ia, index1;
  int noffset[3];
  int isite[3];

  double rd;
  colloid_t * pc;
  colloid_t * colloid_at_site_index(int);

  coords_index_to_ijk(index, isite);

  index1 = coords_index(isite[X] - di[X], isite[Y] - di[Y], isite[Z] - di[Z]);

  assert(cinfo_);
  colloids_info_map(cinfo_, index1, &pc);

  if (pc) {
    coords_nlocal_offset(noffset);
    for (ia = 0; ia < 3; ia++) {
      dn[ia] = 1.0*(noffset[ia] + isite[ia]);
      dn[ia] -= pc->s.r[ia];
    }

    rd = modulus(dn);
    assert(rd > 0.0);
    rd = 1.0/rd;

    for (ia = 0; ia < 3; ia++) {
      dn[ia] *= rd;
    }
  }
  else {
    /* Assume di is the true outward normal (e.g., flat wall) */
    for (ia = 0; ia < 3; ia++) {
      dn[ia] = 1.0*di[ia];
    }
  }

  return;
}

/*****************************************************************************
 *
 *  colloids_q_boundary
 *
 *  Produce an estimate of the surface order parameter Q^0_ab for
 *  normal or planar anchoring.
 *
 *  This will depend on the outward surface normal nhat, and in the
 *  case of planar anchoring may depend on the estimate of the
 *  existing order parameter at the surface Qs_ab.
 *
 *  This planar anchoring idea follows e.g., Fournier and Galatola
 *  Europhys. Lett. 72, 403 (2005).
 *
 *****************************************************************************/

int colloids_q_boundary(const double nhat[3], double qs[3][3],
			double q0[3][3], int map_status) {
  int ia, ib, ic, id;
  int anchoring;

  double qtilde[3][3];
  double amplitude;
  double  nfix[3] = {0.0, 1.0, 0.0};

  assert(map_status == MAP_COLLOID || map_status == MAP_BOUNDARY);

  anchoring = anchoring_coll_;
  if (map_status == MAP_BOUNDARY) anchoring = anchoring_wall_;

  amplitude = blue_phase_amplitude_compute();

  if (anchoring == ANCHORING_FIXED) blue_phase_q_uniaxial(amplitude, nfix, q0);
  if (anchoring == ANCHORING_NORMAL) blue_phase_q_uniaxial(amplitude, nhat, q0);

  if (anchoring == ANCHORING_PLANAR) {

    /* Planar: use the fluid Q_ab to find ~Q_ab */

    for (ia = 0; ia < 3; ia++) {
      for (ib = 0; ib < 3; ib++) {
	qtilde[ia][ib] = qs[ia][ib] + 0.5*amplitude*d_[ia][ib];
      }
    }

    for (ia = 0; ia < 3; ia++) {
      for (ib = 0; ib < 3; ib++) {
	q0[ia][ib] = 0.0;
	for (ic = 0; ic < 3; ic++) {
	  for (id = 0; id < 3; id++) {
	    q0[ia][ib] += (d_[ia][ic] - nhat[ia]*nhat[ic])*qtilde[ic][id]
	      *(d_[id][ib] - nhat[id]*nhat[ib]);
	  }
	}
	/* Return Q^0_ab = ~Q_ab - (1/2) A d_ab */
	q0[ia][ib] -= 0.5*amplitude*d_[ia][ib];
      }
    }

  }

  return 0;
}

/*****************************************************************************
 *
 *  colloids_fix_swd
 *
 *  The velocity gradient tensor used in the Beris-Edwards equations
 *  requires some approximation to the velocity at lattice sites
 *  inside particles. Here we set the lattice velocity based on
 *  the solid body rotation u = v + Omega x rb
 *
 *  Issues: this routines is doing towo things: solid and colloid.
 *  these could be separate.
 *
 *****************************************************************************/

int colloids_fix_swd(colloids_info_t * cinfo, hydro_t * hydro, map_t * map) {

  int ic, jc, kc, index;
  int nlocal[3];
  int noffset[3];
  int status;
  const int nextra = 1;

  double u[3];
  double rb[3];
  double x, y, z;

  colloid_t * p_c;

  assert(cinfo);
  assert(map);

  if (hydro == NULL) return 0;

  coords_nlocal(nlocal);
  coords_nlocal_offset(noffset);

  for (ic = 1 - nextra; ic <= nlocal[X] + nextra; ic++) {
    x = noffset[X] + ic;
    for (jc = 1 - nextra; jc <= nlocal[Y] + nextra; jc++) {
      y = noffset[Y] + jc;
      for (kc = 1 - nextra; kc <= nlocal[Z] + nextra; kc++) {
	z = noffset[Z] + kc;

	index = coords_index(ic, jc, kc);
	map_status(map, index, &status);

	if (status != MAP_FLUID) {
	  u[X] = 0.0;
	  u[Y] = 0.0;
	  u[Z] = 0.0;
	  hydro_u_set(hydro, index, u);
	}

	colloids_info_map(cinfo, index, &p_c);

	if (p_c) {
	  /* Set the lattice velocity here to the solid body
	   * rotational velocity */

	  rb[X] = x - p_c->s.r[X];
	  rb[Y] = y - p_c->s.r[Y];
	  rb[Z] = z - p_c->s.r[Z];

	  cross_product(p_c->s.w, rb, u);

	  u[X] += p_c->s.v[X];
	  u[Y] += p_c->s.v[Y];
	  u[Z] += p_c->s.v[Z];

	  hydro_u_set(hydro, index, u);

	}
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  colloids_q_tensor_anchoring_set
 *
 *****************************************************************************/

void colloids_q_tensor_anchoring_set(const int type) {

  assert(type == ANCHORING_PLANAR || type == ANCHORING_NORMAL);

  anchoring_coll_ = type;

  return;
}

/*****************************************************************************
 *
 *  colloids_q_tensor_anchoring
 *
 *****************************************************************************/

int colloids_q_tensor_anchoring(void) {

  return anchoring_coll_;
}

/*****************************************************************************
 *
 *  wall_anchoring_set
 *
 *****************************************************************************/

void wall_anchoring_set(const int type) {

  assert(type == ANCHORING_PLANAR || type == ANCHORING_NORMAL ||
	 type == ANCHORING_FIXED);

  anchoring_wall_ = type;

  return;
}

/*****************************************************************************
 *
 *  blue_phase_coll_w12
 *
 *****************************************************************************/

int blue_phase_coll_w12(double * w1, double * w2) {

  assert(w1);
  assert(w2);

  *w1 = w1_colloid_;
  *w2 = w2_colloid_;

  return 0;
}

/*****************************************************************************
 *
 *  blue_phase_wall_w12
 *
 *****************************************************************************/

int blue_phase_wall_w12(double * w1, double * w2) {

  assert(w1);
  assert(w2);

  *w1 = w1_wall_;
  *w2 = w2_wall_;

  return 0;
}

/*****************************************************************************
 *
 *  blue_phase_wall_w12_set
 *
 *****************************************************************************/

int blue_phase_wall_w12_set(double w1, double w2) {

  w1_wall_ = w1;
  w2_wall_ = w2;

  return 0;
}

/*****************************************************************************
 *
 *  blue_phase_coll_w12_set
 *
 *****************************************************************************/

int blue_phase_coll_w12_set(double w1, double w2) {

  w1_colloid_ = w1;
  w2_colloid_ = w2;

  return 0;
}

/*****************************************************************************
 *
 *  blue_phase_fs
 *
 *  Compute and return surface free energy area density given
 *    outward normal nhat[3]
 *    fluid Q_ab qs
 *    site map status
 * 
 *****************************************************************************/

int blue_phase_fs(const double dn[3], double qs[3][3], char status,
		  double *fe) {

  int ia, ib;
  double w1, w2;
  double q0[3][3];
  double qtilde;
  double amplitude;
  double f1, f2, s0;

  assert(status == MAP_BOUNDARY || status == MAP_COLLOID);

  colloids_q_boundary(dn, qs, q0, status);

  w1 = w1_colloid_;
  w2 = w2_colloid_;

  if (status == MAP_BOUNDARY) {
    w1 = w1_wall_;
    w2 = w2_wall_;
  }

  amplitude = blue_phase_amplitude_compute(); 
  s0 = 1.5*amplitude;  /* Fournier & Galatola S_0 = (3/2)A */

  /* Free energy density */

  f1 = 0.0;
  f2 = 0.0;
  for (ia = 0; ia < 3; ia++) {
    for (ib = 0; ib < 3; ib++) {
      f1 += (qs[ia][ib] - q0[ia][ib])*(qs[ia][ib] - q0[ia][ib]);
      qtilde = qs[ia][ib] + 0.5*amplitude*d_[ia][ib];
      f2 += (qtilde*qtilde - s0*s0)*(qtilde*qtilde - s0*s0);
    }
  }

  *fe = 0.5*w1*f1 + 0.5*w2*f2;

  return 0;
}

/*****************************************************************************
 *
 *  q_boundary_constants
 *
 *  Compute the comstant term in the cholesteric boundary condition.
 *  Fluid point is (ic, jc, kc) with fluid Q_ab = qs
 *  The outward normal is di[3], and the map status is as given.
 *
 *****************************************************************************/

int q_boundary_constants(int ic, int jc, int kc, double qs[3][3],
			 const int di[3], int status, double c[3][3]) {

  int index;
  int noffset[3];

  int ia, ib, ig, ih;
  int anchor;

  double w1, w2;
  double dnhat[3];
  double qtilde[3][3];
  double q0[3][3];
  double q2 = 0.0;
  double rd;

  double kappa1;
  double q0_chl;
  double amp;

  colloid_t * pc = NULL;

  assert(cinfo_);

  kappa1 = blue_phase_kappa1();
  q0_chl = blue_phase_q0();
  amp    = blue_phase_amplitude_compute();

  /* Default -> outward normal, ie., flat wall */

  w1 = w1_wall_;
  w2 = w2_wall_;
  anchor = anchoring_wall_;

  dnhat[X] = 1.0*di[X];
  dnhat[Y] = 1.0*di[Y];
  dnhat[Z] = 1.0*di[Z];

  if (status == MAP_COLLOID) {

    index = coords_index(ic - di[X], jc - di[Y], kc - di[Z]);
    colloids_info_map(cinfo_, index, &pc);
    assert(pc);

    coords_nlocal_offset(noffset);

    w1 = w1_colloid_;
    w2 = w2_colloid_;
    anchor = anchoring_coll_;

    dnhat[X] = 1.0*(noffset[X] + ic) - pc->s.r[X];
    dnhat[Y] = 1.0*(noffset[Y] + jc) - pc->s.r[Y];
    dnhat[Z] = 1.0*(noffset[Z] + kc) - pc->s.r[Z];

    /* unit vector */
    rd = 1.0/sqrt(dnhat[X]*dnhat[X] + dnhat[Y]*dnhat[Y] + dnhat[Z]*dnhat[Z]);
    dnhat[X] *= rd;
    dnhat[Y] *= rd;
    dnhat[Z] *= rd;
  }


  if (anchor == ANCHORING_NORMAL) {

    for (ia = 0; ia < 3; ia++) {
      for (ib = 0; ib < 3; ib++) {
        q0[ia][ib] = 0.5*amp*(3.0*dnhat[ia]*dnhat[ib] - d_[ia][ib]);
	qtilde[ia][ib] = 0.0;
      }
    }
  }
  else { /* PLANAR */

    q2 = 0.0;
    for (ia = 0; ia < 3; ia++) {
      for (ib = 0; ib < 3; ib++) {
        qtilde[ia][ib] = qs[ia][ib] + 0.5*amp*d_[ia][ib];
        q2 += qtilde[ia][ib]*qtilde[ia][ib];
      }
    }

    for (ia = 0; ia < 3; ia++) {
      for (ib = 0; ib < 3; ib++) {
        q0[ia][ib] = 0.0;
        for (ig = 0; ig < 3; ig++) {
          for (ih = 0; ih < 3; ih++) {
            q0[ia][ib] += (d_[ia][ig] - dnhat[ia]*dnhat[ig])*qtilde[ig][ih]
              *(d_[ih][ib] - dnhat[ih]*dnhat[ib]);
          }
        }

        q0[ia][ib] -= 0.5*amp*d_[ia][ib];
      }
    }
  }

  /* Compute c[a][b] */

  for (ia = 0; ia < 3; ia++) {
    for (ib = 0; ib < 3; ib++) {

      c[ia][ib] = 0.0;

      for (ig = 0; ig < 3; ig++) {
        for (ih = 0; ih < 3; ih++) {
          c[ia][ib] -= kappa1*q0_chl*di[ig]*
	    (e_[ia][ig][ih]*qs[ih][ib] + e_[ib][ig][ih]*qs[ih][ia]);
        }
      }

      /* Normal anchoring: w2 must be zero and q0 is preferred Q
       * Planar anchoring: in w1 term q0 is effectively
       *                   (Qtilde^perp - 0.5S_0) while in w2 we
       *                   have Qtilde appearing explicitly.
       *                   See colloids_q_boundary() etc */

      c[ia][ib] +=
	-w1*(qs[ia][ib] - q0[ia][ib])
	-w2*(2.0*q2 - 4.5*amp*amp)*qtilde[ia][ib];
    }
  }

  return 0;
}

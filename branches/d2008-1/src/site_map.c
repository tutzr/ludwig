/*****************************************************************************
 *
 *  site_map.c
 *
 *  Keeps track of the solid/fluid status of the lattice.
 *
 *  $Id: site_map.c,v 1.1.2.2 2008-01-21 12:23:44 kevin Exp $
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *  (c) 2007 The University of Edinburgh
 *
 *****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pe.h"
#include "coords.h"
#include "io_harness.h"
#include "site_map.h"

char    * site_map;
static struct io_info_t * io_info_site_map;

static void site_map_init_mpi(void);
static void site_map_init_io(void);
static int site_map_read(FILE *, const int, const int, const int);
static int site_map_write(FILE *, const int, const int, const int);
static int site_index(const int, const int, const int);

static int initialised_ = 0;
static MPI_Datatype mpi_xy_t_;
static MPI_Datatype mpi_xz_t_;
static MPI_Datatype mpi_yz_t_;

static int nhalo_ = 1;
static int nlocal_[3];
static int xfac_, yfac_;

/*****************************************************************************
 *
 *  init_site_map
 *
 *  Allocate, and read site information from file if required.
 *
 *****************************************************************************/

void init_site_map() {

  int nh[3];

  get_N_local(nlocal_);

  nh[X] = nlocal_[X] + 2*nhalo_;
  nh[Y] = nlocal_[Y] + 2*nhalo_;
  nh[Z] = nlocal_[Z] + 2*nhalo_;

  xfac_ = nh[Y]*nh[Z];
  yfac_ = nh[Z];

  site_map = (char *) malloc(nh[X]*nh[Y]*nh[Z]*sizeof(char));

  if (site_map == (char *) NULL) fatal("malloc(site_map) failed\n");

  site_map_init_io();
  site_map_init_mpi();
  site_map_set_all(FLUID);

  initialised_ = 1;

  return;
}

/*****************************************************************************
 *
 *  site_map_init_io
 *
 *****************************************************************************/

static void site_map_init_io() {

  io_info_site_map = io_info_create();
  io_info_set_name(io_info_site_map, "Site map information");
  io_info_set_read(io_info_site_map, site_map_read);
  io_info_set_write(io_info_site_map, site_map_write);
  io_info_set_bytesize(io_info_site_map, sizeof(char));
  io_info_set_decomposition(io_info_site_map, 1);

  return;
}

/*****************************************************************************
 *
 *  site_map_init_mpi
 *
 *****************************************************************************/

static void site_map_init_mpi() {

  int nh[3];

  nh[X] = nlocal_[X] + 2*nhalo_;
  nh[Y] = nlocal_[Y] + 2*nhalo_;
  nh[Z] = nlocal_[Z] + 2*nhalo_;

  /* YZ planes in the X direction */
  MPI_Type_vector(1, nh[Y]*nh[Z]*nhalo_, 1, MPI_CHAR, &mpi_yz_t_);
  MPI_Type_commit(&mpi_yz_t_);

  /* XZ planes in the Y direction */
  MPI_Type_vector(nh[X], nh[Z]*nhalo_, nh[Y]*nh[Z], MPI_CHAR, &mpi_xz_t_);
  MPI_Type_commit(&mpi_xz_t_);

  /* XY planes in Z direction */
  MPI_Type_vector(nh[X]*nh[Y], nhalo_, nh[Z], MPI_CHAR, &mpi_xy_t_);
  MPI_Type_commit(&mpi_xy_t_);

  return;
}

/*****************************************************************************
 *
 *  finish_site_map
 *
 *  Clean up.
 *
 *****************************************************************************/

void finish_site_map() {

  assert(initialised_);

  free(site_map);
  io_info_destroy(io_info_site_map);

  MPI_Type_free(&mpi_xy_t_);
  MPI_Type_free(&mpi_xz_t_);
  MPI_Type_free(&mpi_yz_t_);

  initialised_ = 0;

  return;
}

/*****************************************************************************
 *
 *  site_map_set_all
 *
 *  Set all sites to the given status.
 *
 *****************************************************************************/

void site_map_set_all(char status) {

  int index, ic, jc, kc;

  assert(initialised_);
  assert(status >= FLUID && status <= BOUNDARY);

  for (ic = 1 - nhalo_; ic <= nlocal_[X] + nhalo_; ic++) {
    for (jc = 1 - nhalo_; jc <= nlocal_[Y] + nhalo_; jc++) {
      for (kc = 1 - nhalo_; kc <= nlocal_[Z] + nhalo_; kc++) {
	index = site_index(ic, jc, kc);
	site_map[index] = status;
      }
    }
  }

  return;
}

/*****************************************************************************
 *
 *  site_map_get_status
 *
 *  Return the site status at local index (ic, jc, kc).
 *
 *****************************************************************************/

char site_map_get_status(int ic, int jc, int kc) {

  int index;

  assert(initialised_);

  index = site_index(ic, jc, kc);

  return site_map[index];
}

/*****************************************************************************
 *
 *  site_map_set_status
 *
 *  Set the status at (ic, jc, kc).
 *
 *****************************************************************************/

void site_map_set_status(int ic, int jc, int kc, char status) {

  int index;

  assert(initialised_);
  assert(status >= FLUID && status <= BOUNDARY);

  index = site_index(ic, jc, kc);
  site_map[index] = status;

  return;
}

/*****************************************************************************
 *
 *  site_map_volume
 *
 *  What is the current fluid volume? Call this with status = FLUID.
 *
 *  The value is computed as a double.
 *
 *****************************************************************************/

double site_map_volume(char status) {

  double  v_local, v_total;
  int     index, ic, jc, kc;

  assert(initialised_);
  assert(status >= FLUID && status <= BOUNDARY);

  v_local = 0.0;

  /* Look for fluid nodes (not halo) */

  for (ic = 1; ic <= nlocal_[X]; ic++) {
    for (jc = 1; jc <= nlocal_[Y]; jc++) {
      for (kc = 1; kc <= nlocal_[Z]; kc++) {

	index = site_index(ic, jc, kc);
	if (site_map[index] == status) v_local += 1.0;
      }
    }
  }

  /* All processes get the total */

  MPI_Allreduce(&v_local, &v_total, 1, MPI_DOUBLE, MPI_SUM, cart_comm());

  return v_total;
}

/*****************************************************************************
 *
 *  site_map_halo
 *
 *  Swap the site_map values.
 *
 *****************************************************************************/

void site_map_halo() {

  int ic, jc, kc, ihalo, ireal, nh;
  int back, forw;
  const int btag = 151;
  const int ftag = 152;
  MPI_Comm comm = cart_comm();
  MPI_Request request[4];
  MPI_Status status[4];

  assert(initialised_);

  /* YZ planes in X direction */

  if (cart_size(X) == 1) {
    for (nh = 0; nh < nhalo_; nh++) {
      for (jc = 1; jc <= nlocal_[Y]; jc++) {
	for (kc = 1 ; kc <= nlocal_[Z]; kc++) {
	  ihalo = site_index(1-nh, jc, kc);
	  ireal = site_index(nlocal_[X]-nh, jc, kc);
	  site_map[ihalo] = site_map[ireal];

	  ihalo = site_index(nlocal_[X]+1+nh, jc, kc);
	  ireal = site_index(1+nh, jc, kc);
	  site_map[ihalo] = site_map[ireal];
	}
      }
    }
  }
  else {

    back = cart_neighb(BACKWARD, X);
    forw = cart_neighb(FORWARD, X);

    ihalo = site_index(nlocal_[X] + nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_yz_t_, forw, btag, comm, request);
    ihalo = site_index(1-nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_yz_t_, back, ftag, comm, request+1);
    ireal = site_index(1, 1-nhalo_, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_yz_t_, back, btag, comm, request+2);
    ireal = site_index(nlocal_[X] - nhalo_ + 1, 1-nhalo_, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_yz_t_, forw, ftag, comm, request+3);
    MPI_Waitall(4, request, status);
  }

  /* XZ planes in the Y direction */

  if (cart_size(Y) == 1) {
    for (nh = 0; nh < nhalo_; nh++) {
      for (ic = 1-nhalo_; ic <= nlocal_[X] + nhalo_; ic++) {
	for (kc = 1; kc <= nlocal_[Z]; kc++) {
	  ihalo = site_index(ic, 1-nh, kc);
	  ireal = site_index(ic, nlocal_[Y]-nh, kc);
	  site_map[ihalo] = site_map[ireal];

	  ihalo = site_index(ic, nlocal_[Y]+1+nh, kc);
	  ireal = site_index(ic, 1+nh, kc);
	  site_map[ihalo] = site_map[ireal];
	}
      }
    }
  }
  else {

    back = cart_neighb(BACKWARD, Y);
    forw = cart_neighb(FORWARD, Y);

    ihalo = site_index(1-nhalo_, nlocal_[Y] + nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_xz_t_, forw, btag, comm, request);
    ihalo = (1-nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ihalo,  1, mpi_xz_t_, back, ftag, comm, request+1);
    ireal = site_index(1-nhalo_, 1, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_xz_t_, back, btag, comm, request+2);
    ireal = site_index(1-nhalo_, nlocal_[Y] - nhalo_ + 1, 1-nhalo_);
    MPI_Issend(site_map + ireal, 1, mpi_xz_t_, forw, ftag, comm, request+3);
    MPI_Waitall(4, request, status);
  }

  /* XY planes in the Z direction */

  if (cart_size(Z) == 1) {
    for (nh = 0; nh < nhalo_; nh++) {
      for (ic = 1 - nhalo_; ic <= nlocal_[X] + nhalo_; ic++) {
	for (jc = 1 - nhalo_; jc <= nlocal_[Y] + nhalo_; jc++) {
	  ihalo = site_index(ic, jc, 1-nh);
	  ireal = site_index(ic, jc, nlocal_[Z]-nh);
	  site_map[ihalo] = site_map[ireal];

	  ihalo = site_index(ic, jc, nlocal_[Z]+1+nh);
	  ireal = site_index(ic, jc,            1+nh);
	  site_map[ihalo] = site_map[ireal];
	}
      }
    }
  }
  else {

    back = cart_neighb(BACKWARD, Z);
    forw = cart_neighb(FORWARD, Z);

    ihalo = site_index(1-nhalo_, 1-nhalo_, nlocal_[Z] + nhalo_);
    MPI_Irecv(site_map + ireal,  1, mpi_xy_t_, forw, btag, comm, request);
    ihalo = site_index(1-nhalo_, 1-nhalo_, 1-nhalo_);
    MPI_Irecv(site_map + ireal,  1, mpi_xy_t_, back, ftag, comm, request+1);
    ireal = site_index(1-nhalo_, 1-nhalo_, 1);
    MPI_Issend(site_map + ireal, 1, mpi_xy_t_, back, btag, comm, request+2);
    ireal = site_index(1-nhalo_, 1-nhalo_, nlocal_[Z] - nhalo_ + 1);
    MPI_Issend(site_map + ireal, 1, mpi_xy_t_, forw, ftag, comm, request+3);
    MPI_Waitall(4, request, status);
  }

  return;
}

/*****************************************************************************
 *
 *  site_map_write
 *
 *  Write function.
 *
 *****************************************************************************/

static int site_map_write(FILE * fp, int ic, int jc, int kc) {

  int index, n;

  index = site_index(ic, jc, kc);

  n = fputc(site_map[index], fp);
  if (n == EOF) fatal("Failed to write site map at %d\n", index);

  return 1;
}

/*****************************************************************************
 *
 *  site_map_read
 *
 *****************************************************************************/

static int site_map_read(FILE * fp, int ic, int jc, int kc) {

  int index, n;

  index = site_index(ic, jc, kc);

  n = fgetc(fp);
  if (n == EOF) fatal("Failed to read site map at %d\n", index);

  site_map[index] = n;

  return 1;
}

/*****************************************************************************
 *
 *  site_index
 *
 *  Compute the index from coordinates ic, jc, kc.
 *
 *****************************************************************************/

static int site_index(const int ic, const int jc, const int kc) {

  assert(ic >= 1-nhalo_);
  assert(jc >= 1-nhalo_);
  assert(kc >= 1-nhalo_);
  assert(ic <= nlocal[X] + nhalo_);
  assert(jc <= nlocal[Y] + nhalo_);
  assert(kc <= nlocal[Z] = nhalo_);

  return (xfac_*(nhalo_ + ic - 1) + yfac_*(nhalo_ + jc -1) + nhalo_ + kc - 1);
}

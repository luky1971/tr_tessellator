/*
 * Copyright Ahnaf Siddiqui and Sameer Varma.
 */

#include "ll_tessellation.h"

#include <float.h>
#include <stdio.h>
#include "smalloc.h"
#include "vec.h"

#include "gkut_io.h"
#include "gkut_log.h"

real tessellate_area(const char *traj_fname, const char *ndx_fname, real cell_width, output_env_t *oenv) {
	rvec **pre_x, **x;
	int nframes, natoms;

	read_traj(traj_fname, &pre_x, &nframes, &natoms, oenv);

	// Filter trajectory by index file if present
	if(ndx_fname != NULL) {
		const int NUMGROUPS = 1;
		int *isize;
		atom_id **indx;
		char **grp_names;

		snew(isize, NUMGROUPS);
		snew(indx, NUMGROUPS);
		snew(grp_names, NUMGROUPS);

		rd_index(ndx_fname, NUMGROUPS, isize, indx, grp_names);
		sfree(grp_names);

		natoms = isize[0];
		sfree(isize);

		snew(x, nframes);
		for(int i = 0; i < nframes; ++i) {
			snew(x[i], natoms);
			for(int j = 0; j < natoms; ++j) {
				copy_rvec(pre_x[i][indx[0][j]], x[i][j]);
			}
		}

		// free memory
		sfree(indx[0]);
		sfree(indx);

		for(int i = 0; i < nframes; ++i) {
			sfree(pre_x[i]);
		}
		sfree(pre_x);
	}
	else {
		x = pre_x;
	}

	f_tessellate_area(x, nframes, natoms, cell_width);

	// free memory
	for(int i = 0; i < nframes; ++i) {
		sfree(x[i]);
	}
	sfree(x);

	return 0;
}

real f_tessellate_area(rvec **x, int nframes, int natoms, real cell_width) {
	struct weighted_grid grid;

	construct_grid(x, nframes, natoms, cell_width, &grid);

#ifdef LLT_DEBUG
	print_log("Grid: \n");
	print_log("dimx = %d, dimy = %d, dimz = %d\n", grid.dimx, grid.dimy, grid.dimz);
	print_log("cell width = %f\n", grid.cell_width);
	print_log("minx = %f, miny = %f, minz = %f\n", grid.minx, grid.miny, grid.minz);
#endif

	load_grid(x, nframes, natoms, &grid);

	// free memory
	free_grid(&grid);
}

void construct_grid(rvec **x, int nframes, int natoms, real cell_width, struct weighted_grid *grid) {
	real minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX, 
		maxx = FLT_MIN, maxy = FLT_MIN, maxz = FLT_MIN;
	int dimx, dimy, dimz;

	for(int fr = 0; fr < nframes; ++fr) {
		for(int a = 0; a < natoms; ++a) {
			if(x[fr][a][XX] < minx)	minx = x[fr][a][XX];
			if(x[fr][a][XX] > maxx)	maxx = x[fr][a][XX];

			if(x[fr][a][YY] < miny)	miny = x[fr][a][YY];
			if(x[fr][a][YY] > maxy)	maxy = x[fr][a][YY];

			if(x[fr][a][ZZ] < minz)	minz = x[fr][a][ZZ];
			if(x[fr][a][ZZ] > maxz)	maxz = x[fr][a][ZZ];
		}
	}

	// # weights in each dim is the # grid cells - 1 + an extra grid cell (bc of int cast floor) + 1 for the last grid point
	dimx = ((int)((maxx - minx)/cell_width) + 2);
	dimy = ((int)((maxy - miny)/cell_width) + 2);
	dimz = ((int)((maxz - minz)/cell_width) + 2);

	snew(grid->weights, dimx * dimy * dimz);
	grid->dimx = dimx, grid->dimy = dimy, grid->dimz = dimz;
	grid->cell_width = cell_width;
	grid->minx = minx, grid->miny = miny, grid->minz = minz;
#ifdef LLT_DEBUG
	print_log("maxx = %f, maxy = %f, maxz = %f\n", maxx, maxy, maxz);
#endif
}

void load_grid(rvec **x, int nframes, int natoms, struct weighted_grid *grid) {
	real *weights = grid->weights;
	int dimx = grid->dimx, dimy = grid->dimy, dimz = grid->dimz;
	real cell_width = grid->cell_width;
	real minx = grid->minx, miny = grid->miny, minz = grid->minz;

	real diag_sq = 3 * cell_width * cell_width;
	rvec grid_point;
	int xi, yi, zi;
	int dimyz = dimy * dimz;

	for(int fr = 0; fr < nframes; ++fr) {
		for(int a = 0; a < natoms; ++a) {
			// Indices of the origin point of the grid cell surrounding this atom
			xi = (int)((x[fr][a][XX] - minx)/cell_width);
			yi = (int)((x[fr][a][YY] - miny)/cell_width);
			zi = (int)((x[fr][a][ZZ] - minz)/cell_width);

			// Load the eight grid points around this atom. Closer distance to atom = higher weight
			grid_point[XX] = minx + xi * cell_width, 
				grid_point[YY] = miny + yi * cell_width, 
				grid_point[ZZ] = minz + zi * cell_width;
			*(weights + xi * dimyz + yi * dimz + zi) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[XX] += cell_width;
			*(weights + (xi+1) * dimyz + yi * dimz + zi) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[YY] += cell_width;
			*(weights + (xi+1) * dimyz + (yi+1) * dimz + zi) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[XX] -= cell_width;
			*(weights + xi * dimyz + (yi+1) * dimz + zi) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[ZZ] += cell_width;
			*(weights + xi * dimyz + (yi+1) * dimz + zi+1) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[YY] -= cell_width;
			*(weights + xi * dimyz + yi * dimz + zi+1) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[XX] += cell_width;
			*(weights + (xi+1) * dimyz + yi * dimz + zi+1) += diag_sq - distance2(x[fr][a], grid_point);
			grid_point[YY] += cell_width;
			*(weights + (xi+1) * dimyz + (yi+1) * dimz + zi+1) += diag_sq - distance2(x[fr][a], grid_point);
		}
	}

#ifdef LLT_DEBUG
	print_log("Weights: \n");
	for(int i = 0; i < dimx * dimyz; i++) {
		print_log("%f ", *(weights + i));
	}
	print_log("\n");
#endif
}

void free_grid(struct weighted_grid *grid) {
	sfree(grid->weights);
}

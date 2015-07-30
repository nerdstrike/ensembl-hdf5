// LICENSE
//
//  Copyright (c) 1999-2015 The European Bioinformatics Institute and
//  Genome Research Limited.  All rights reserved.
//
//  This software is distributed under a modified Apache license.
//  For license details, please see
//
// http://www.ensembl.org/info/about/code_licence.html
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdf5.h"
#include "hdf5_wrapper.h"

static int BIG_DIM_LENGTH = 1000;
static bool DEBUG = false;

////////////////////////////////////////////////////////
// Generic array functions
////////////////////////////////////////////////////////

static hsize_t volume(hsize_t rank, hsize_t * dim_sizes) {
	hsize_t dim;
	hsize_t res = 1;
	for (dim = 0; dim < rank; dim++)
		res *= dim_sizes[dim];
	return res;
}

static void * alloc_ndim_array(hsize_t rank, hsize_t * dim_sizes, size_t elem_size) {
	return calloc(1 + volume(rank, dim_sizes), elem_size);
}

////////////////////////////////////////////////////////
// String arrays 
// A string array is simply an array of equal length strings
// all stored on a single contiguous block which was allocated
// in a single calloc command; also they are useful to 
// hold the buffer for the H5D{write|read} functions
////////////////////////////////////////////////////////

static StringArray * new_string_array(hsize_t count, hsize_t length) {
	StringArray * sarray = calloc(1, sizeof(StringArray));
	sarray->count = count;
	sarray->length = length;
	sarray->array = calloc((length + 1)* count, sizeof(char));
	return sarray;
}

char * get_string_in_array(StringArray * sarray, hsize_t index) {
	return sarray->array + index * (sarray->length + 1);
}

void destroy_string_array(StringArray * sarray) {
	free(sarray->array);
	free(sarray);
}

////////////////////////////////////////////////////////
// String array functions
////////////////////////////////////////////////////////

static hsize_t max_string_length(char ** strings, hsize_t count) {
	hsize_t index, max;

	for (index = 0; index < count; index++) {
		hsize_t length = strlen(strings[index]);
		if (index == 0 || length > max)
			max = length;
	}

	return max;
}

static StringArray * normalize_strings(char ** strings, hsize_t count, hsize_t length) {
	StringArray * sarray = new_string_array(count, length);
	hsize_t index;

	for (index = 0; index < count; index++)
		strcpy(get_string_in_array(sarray, index), strings[index]);

	return sarray;
}

static void store_string_array(hid_t file, char * dataset_name, hsize_t count, char ** strings) {
	hsize_t max_length = max_string_length(strings, count);
	hsize_t shape[2];
	shape[0] = count;
	shape[1] = max_length + 1;
	if (DEBUG)
		printf("Storing %lli names in %s:%llix%lli\n", count, dataset_name, shape[0], shape[1]);
	hid_t dataspace = H5Screate_simple(2, shape, NULL);
	hid_t dataset = H5Dcreate(file, dataset_name, H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	StringArray * sarray = normalize_strings(strings, count, max_length);
	H5Dwrite(dataset, H5T_NATIVE_CHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, sarray->array);
	destroy_string_array(sarray);
	H5Sclose(dataspace);
	H5Dclose(dataset);
}

static StringArray * get_string_array(hid_t file, char * dataset_name) {
	hid_t dataset = H5Dopen(file, dataset_name, H5P_DEFAULT);
	hsize_t dim_sizes[2];
	hid_t dataspace = H5Dget_space(dataset);
	H5Sget_simple_extent_dims(dataspace, dim_sizes, NULL);
	H5Sclose(dataspace);
	StringArray * dim_names = new_string_array(dim_sizes[0], dim_sizes[1]-1);
	H5Dread(dataset, H5T_NATIVE_CHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, dim_names->array);
	H5Dclose(dataset);
	return dim_names;
}

static StringArray * get_string_subarray(hid_t file, char * dataset_name, hsize_t offset, hsize_t count) {
	hid_t dataset = H5Dopen(file, dataset_name, H5P_DEFAULT);
	hsize_t width[2];
	hid_t dataspace = H5Dget_space(dataset);
	H5Sget_simple_extent_dims(dataspace, width, NULL);
	width[0] = count;
	StringArray * dim_names = new_string_array(count, width[1] - 1);
	hsize_t offset2[2];
	offset2[0] = offset;
	offset2[1] = 0;
	if (DEBUG)
		printf("Querying names in %s: %lli-%lli\n", dataset_name, offset, offset + count);
	H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset2, NULL, width, NULL);
	H5Dread(dataset, H5T_NATIVE_CHAR, H5S_ALL, dataspace, H5P_DEFAULT, dim_names->array);
	if (DEBUG) {
		int i;
		for (i = 0; i < dim_names->count; i++)
			printf("%lli => %s\n", offset + i, get_string_in_array(dim_names, i));
	}
	H5Sclose(dataspace);
	H5Dclose(dataset);
	return dim_names;
}

////////////////////////////////////////////////////////
// Dim names 
////////////////////////////////////////////////////////

static void store_dim_names(hid_t file, hsize_t rank, char ** strings) {
	if (DEBUG) {
		printf("Storing dim names\n");
		int i;
		for (i = 0; i < rank; i++)
			printf("%s\n", strings[i]);
	}
	store_string_array(file, "/dim_names", rank, strings);
}

StringArray * get_dim_names(hid_t file) {
	StringArray * sa = get_string_array(file, "/dim_names");
	if (DEBUG) {
		printf("Reading dim names\n");
		int i;
		for (i = 0; i < sa->count; i++)
			printf("%i => %s\n", i, get_string_in_array(sa, i));
	}
	return sa;
}

////////////////////////////////////////////////////////
// Dim labels
////////////////////////////////////////////////////////

static void store_dim_labels(hid_t group, hsize_t dim, hsize_t dim_size, char ** strings) {
	char buf[5];
	sprintf(buf, "%llu", dim);
	store_string_array(group, buf, dim_size, strings);
}

static void store_all_dims_labels(hid_t file, hsize_t rank, hsize_t * dim_sizes, char *** dim_labels) {
	hsize_t dim;
	hid_t group = H5Gcreate(file, "/dim_labels", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	for (dim = 0; dim < rank; dim++)
		store_dim_labels(group, dim, dim_sizes[dim], dim_labels[dim]);
	H5Gclose(group);
}

static StringArray * get_dim_labels(hid_t group, hsize_t dim, hsize_t offset, hsize_t width) {
	char buf[5];
	sprintf(buf, "%llu", dim);
	return get_string_subarray(group, buf, offset, width);
}

static StringArray ** get_table_dims_labels(hid_t file, ResultTable * table, hsize_t * offset, hsize_t * width) {
	StringArray ** dim_labels = calloc(table->columns, sizeof(StringArray*));
	hid_t dim;

	hid_t group = H5Gopen(file, "/dim_labels", H5P_DEFAULT);
	for (dim = 0; dim < table->columns; dim++)
		dim_labels[dim] = get_dim_labels(group, dim, offset[table->dims[dim]], width[table->dims[dim]]);
	H5Gclose(group);

	return dim_labels;
}

StringArray * get_all_dim_labels(hid_t file, hsize_t dim) {
	char buf[5];
	sprintf(buf, "%llu", dim);
	hid_t group = H5Gopen(file, "/dim_labels", H5P_DEFAULT);
	return get_string_array(group, buf);
}

////////////////////////////////////////////////////////
// File info 
////////////////////////////////////////////////////////

hsize_t get_file_rank(hid_t file) {
	hid_t dataset = H5Dopen(file, "/matrix", H5P_DEFAULT);
	hid_t dataspace = H5Dget_space(dataset);
	hsize_t rank = H5Sget_simple_extent_ndims(dataspace);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	return rank;
}

static void set_file_core_rank(hid_t file, hsize_t core_rank) {
	hid_t dataset = H5Dopen(file, "/matrix", H5P_DEFAULT);
        hid_t aid2  = H5Screate(H5S_SCALAR);
        hid_t attr2 = H5Acreate(dataset, "Core dimensions", H5T_NATIVE_INT, aid2,
                      H5P_DEFAULT, H5P_DEFAULT);
	if (DEBUG)
		printf("Setting core rank %lli\n", core_rank);
        H5Awrite(attr2, H5T_NATIVE_INT, &core_rank);
	H5Dclose(dataset);
        H5Aclose(attr2);
        H5Sclose(aid2);
}

hsize_t get_file_core_rank(hid_t file) {
	hsize_t core_rank;
	hid_t attr = H5Aopen_by_name(file, "/matrix", "Core dimensions", H5P_DEFAULT, H5P_DEFAULT);
	H5Aread(attr, H5T_NATIVE_INT, &core_rank);
	H5Aclose(attr);
	return core_rank;
}

////////////////////////////////////////////////////////
// Matrix operations 
////////////////////////////////////////////////////////

static void create_matrix(hid_t file, hsize_t rank, hsize_t * dim_sizes, hsize_t * chunk_sizes) {
	hid_t dataspace = H5Screate_simple(rank, dim_sizes, NULL);
	hid_t cparms = H5Pcreate(H5P_DATASET_CREATE);
	H5Pset_chunk(cparms, rank, chunk_sizes);
	hid_t dataset = H5Dcreate(file, "/matrix", H5T_NATIVE_DOUBLE, dataspace,
		            H5P_DEFAULT, cparms, H5P_DEFAULT);
	H5Sclose(dataspace);
	H5Pclose(cparms);
	H5Dclose(dataset);
}

static void store_values_in_matrix(hid_t file, hsize_t count, hsize_t ** coords, double * values) {
	hsize_t rank = get_file_rank(file);
	hid_t dataset = H5Dopen(file, "/matrix", H5P_DEFAULT);
 	hid_t filespace = H5Dget_space(dataset);
	hid_t memspace = H5Screate_simple(1, &count, NULL);
	hsize_t * coord = calloc(count * rank, sizeof(hsize_t));
	hsize_t index, dim, pos;
	pos = 0;
	for (index = 0; index < count; index++) {
		for (dim = 0; dim < rank; dim++) {
			coord[pos++] = coords[index][dim];
		}
	}
	H5Sselect_elements(filespace, H5S_SELECT_SET, count, coord);
	H5Dwrite(dataset, H5T_NATIVE_DOUBLE, memspace, filespace, H5P_DEFAULT, values);
	H5Sclose(filespace);
	H5Sclose(memspace);
	H5Dclose(dataset);
}

static double * fetch_values(hid_t file, hsize_t * offset, hsize_t * width) {
	hsize_t rank = get_file_rank(file);
	double * array = alloc_ndim_array(rank, width, H5Tget_size(H5T_NATIVE_DOUBLE));
	hid_t dataset = H5Dopen(file, "/matrix", H5P_DEFAULT);
	hid_t dataspace = H5Dget_space(dataset);
	H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, width, NULL);
	H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, dataspace, H5P_DEFAULT, array);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	return array;
}

////////////////////////////////////////////////////////
// Boundaries
////////////////////////////////////////////////////////

static void create_boundaries_group(hid_t group, hsize_t core_rank, hsize_t dim, hsize_t dim_size) {
	hsize_t shape[3];
	shape[0] = dim_size;
	shape[1] = core_rank - 1;
	shape[2] = 2;
	hid_t dataspace = H5Screate_simple(3, shape, NULL);

	hid_t params = H5Pcreate(H5P_DATASET_CREATE);
	long value = -1;
	H5Pset_fill_value(params, H5T_NATIVE_LONG, &value);

	char buf[5];
	sprintf(buf, "%llu", dim);
	hid_t dataset = H5Dcreate(group, buf, H5T_NATIVE_LONG, dataspace, H5P_DEFAULT, params, H5P_DEFAULT);
	H5Dclose(dataset);
	H5Sclose(dataspace);
}

static void create_boundaries(hid_t file, hsize_t rank, hsize_t core_rank, hsize_t * dim_sizes) {
	int dim;
	hid_t group = H5Gcreate(file, "/boundaries", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	for (dim = rank - core_rank; dim < rank; dim++)
		create_boundaries_group(group, core_rank, dim, dim_sizes[dim - rank + core_rank]);
	H5Gclose(group);
}

static long * initialise_boundary_array_dim(hid_t file, hsize_t dim) {
	char buf[5];
	hsize_t shape[3];
	hid_t group = H5Gopen(file, "/boundaries", H5P_DEFAULT);
	sprintf(buf, "%llu", dim);
	hid_t dataset = H5Dopen(group, buf, H5P_DEFAULT);
	hid_t dataspace = H5Dget_space(dataset);
	H5Sget_simple_extent_dims(dataspace, shape, NULL);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Gclose(group);
	return calloc(volume(3, shape), sizeof(long));
}

static long ** initialise_boundary_array(hid_t file, hsize_t core_rank) {
	long ** res = calloc(core_rank, sizeof(long *));
	hsize_t dim;
	for (dim = 0; dim < core_rank; dim++)
		res[dim] = initialise_boundary_array_dim(file, dim);
	return res;
}

static void compute_boundaries_row_dim_2(long * boundaries, hsize_t rank, hsize_t core_rank, hsize_t * coords, hsize_t dim, hsize_t dim2) {
	if (DEBUG)
		printf("Entering position at (dim%lli:%lli;dim%lli:%lli)\n", dim, coords[dim], dim2, coords[dim2]);
	hsize_t proj_dim2 = dim2 > dim? dim2 - 1 + core_rank - rank: dim + core_rank - rank;
	hsize_t position = coords[dim] * (core_rank - 1) * 2 + proj_dim2 * 2;

	if (DEBUG)
		printf("Boundaries on dim%lli:%li-%li\n", dim2, boundaries[position], boundaries[position+1]);

	if (coords[dim2] < boundaries[position] || boundaries[position] < 0)
		boundaries[position] = coords[dim2];

	if (coords[dim2] + 1 > boundaries[position + 1])
		boundaries[position + 1] = coords[dim2] + 1;

	if (DEBUG)
		printf("New boundaries on dim%lli:%li-%li\n", dim2, boundaries[position], boundaries[position+1]);
}

static void compute_boundaries_row(long ** boundaries, hsize_t rank, hsize_t core_rank, hsize_t * coords) {
	hsize_t dim, dim2;

	for (dim = rank - core_rank; dim < rank; dim++) {
		for (dim2 = rank - core_rank; dim2 < dim; dim2++) {
			compute_boundaries_row_dim_2(boundaries[dim - rank + core_rank], rank, core_rank, coords, dim, dim2);
			compute_boundaries_row_dim_2(boundaries[dim2 - rank + core_rank], rank, core_rank, coords, dim2, dim);
		}
	}
}

static void compute_boundaries(long ** boundaries, hsize_t rank, hsize_t core_rank, hsize_t count, hsize_t ** coords) {
	hsize_t row;

	for (row = 0; row < count; row++)
		compute_boundaries_row(boundaries, rank, core_rank, coords[row]);
}

static void store_boundaries_group(hid_t group, hsize_t dim, long * boundaries) {
	char buf[5];
	sprintf(buf, "%llu", dim);
	hid_t dataset = H5Dopen(group, buf, H5P_DEFAULT);
	H5Dwrite(dataset, H5T_NATIVE_LONG, H5S_ALL, H5S_ALL, H5P_DEFAULT, boundaries);
	H5Dclose(dataset);
}

static void store_boundaries(hid_t file, hsize_t rank, hsize_t core_rank, long ** boundaries) {
	int dim;
	hid_t group = H5Gopen(file, "/boundaries", H5P_DEFAULT);
	for (dim = 0; dim < core_rank; dim++)
		store_boundaries_group(group, dim, boundaries[dim]);
	H5Gclose(group);
}

static void free_boundary_array(long ** array, hsize_t core_rank) {
	hsize_t dim;
	for (dim = 0; dim < core_rank; dim++)
		free(array[dim]);
	free(array);
}

static void set_boundaries(hid_t file, hsize_t count, hsize_t ** coords) {
	hsize_t rank = get_file_rank(file);
	hsize_t core_rank = get_file_core_rank(file);
	long ** boundaries = initialise_boundary_array(file, core_rank);
	compute_boundaries(boundaries, rank, core_rank, count, coords);
	store_boundaries(file, rank, core_rank, boundaries);
	free_boundary_array(boundaries, core_rank);
}

////////////////////////////////////////////////////////
// Extracting boundary information 
////////////////////////////////////////////////////////

static long * open_boundaries_dim(hid_t group, hsize_t rank, hsize_t core_rank, hsize_t dim, hsize_t constraint) {
	char buf[5];
	sprintf(buf, "%llu", dim);
	hid_t dataset = H5Dopen(group, buf, H5P_DEFAULT);
	hsize_t offset[3];
	offset[0] = constraint;
	offset[1] = 0;
	offset[2] = 0;
	hsize_t width[3];
	width[0] = 1;
	width[1] = core_rank - 1;
	width[2] = 2;
	hid_t dataspace = H5Dget_space(dataset);
	H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, width, NULL);
	long * res = alloc_ndim_array(3, width, H5Tget_size(H5T_NATIVE_LONG));
	H5Dread(dataset, H5T_NATIVE_LONG, H5S_ALL, dataspace, H5P_DEFAULT, res);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	return res;
}

static long ** open_boundaries(hid_t file, hsize_t rank, hsize_t core_rank, bool * set_dims, hsize_t * constraints) {
	long ** boundaries = calloc(core_rank, sizeof(long *));
	hid_t group = H5Gopen(file, "boundaries", H5P_DEFAULT);
	hsize_t dim;
	for (dim = rank - core_rank; dim < rank; dim++)
		if (set_dims[dim])
			boundaries[dim] = open_boundaries_dim(group, rank, core_rank, dim, constraints[dim]);
	H5Gclose(group);
	return boundaries;
}

static hsize_t lower_search_bound(hsize_t rank, hsize_t core_rank, hsize_t dim, long ** boundaries) {
	hsize_t dim2;
	hsize_t offset = 0;
	for (dim2 = rank - core_rank; dim2 < rank; dim2++) {
		if (boundaries[dim2]) {
			hsize_t proj_dim = dim2 < dim? dim - 1 + core_rank - rank: dim + core_rank - rank;
			hsize_t min = boundaries[dim2][2 * proj_dim];
			if (min > offset)
				offset = min;
		}
	}
	return offset;
}

static hsize_t upper_search_bound(hsize_t rank, hsize_t core_rank, hsize_t dim, hsize_t dim_size, long ** boundaries) {
	hsize_t dim2;
	hsize_t upper = dim_size;
	for (dim2 = rank - core_rank; dim2 < rank; dim2++) {
		if (boundaries[dim2]) {
			hsize_t proj_dim = dim2 < dim? dim - 1 + core_rank - rank: dim + core_rank - rank;
			hsize_t max = boundaries[dim2][2 * proj_dim + 1];
			if (max < upper)
				upper = max;
		}
	}
	return upper;
}

static bool set_query_parameters(hid_t file, hsize_t rank, bool * set_dims, hsize_t * constraints, hsize_t * offset, hsize_t * width) {
	int core_rank = get_file_core_rank(file);
	hsize_t * dim_sizes = calloc(rank, sizeof(hsize_t));
	hid_t dataset = H5Dopen(file, "/matrix", H5P_DEFAULT);
	hid_t dataspace = H5Dget_space(dataset);
	H5Sget_simple_extent_dims(dataspace, dim_sizes, NULL);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	long ** boundaries = open_boundaries(file, rank, core_rank, set_dims, constraints);

	hsize_t dim;
	if (DEBUG)
		printf("Total of %lli dimensions, %i core\n", rank, core_rank);
	for (dim = 0; dim < rank; dim++) {
		if (set_dims[dim]) {
			offset[dim] = constraints[dim];
			width[dim] = 1;
			if (DEBUG)
				printf("Constrained dim %lli searched from %lli -> %lli\n", dim, offset[dim], offset[dim]+width[dim]);
		} else if (dim >= rank - core_rank) {
			offset[dim] = lower_search_bound(rank, core_rank, dim, boundaries);
			width[dim] = upper_search_bound(rank, core_rank, dim, dim_sizes[dim], boundaries) - offset[dim];
			if (DEBUG)
				printf("Bounded dim %lli searched from %lli -> %lli\n", dim, offset[dim], offset[dim]+width[dim]);
		} else {
			offset[dim] = 0;
			width[dim] = dim_sizes[dim];
			if (DEBUG)
				printf("Free dim %lli searched from %lli -> %lli\n", dim, offset[dim], offset[dim]+width[dim]);
		}
	}

	if (DEBUG) {
		printf("About to explore a field of (");
		for (dim = 0; dim < rank; dim++)
			printf("%llix", width[dim]);
		puts(") values;");
	}

	// Cleaning up
	for (dim = 0; dim < core_rank; dim++)
		if (set_dims[dim + rank - core_rank])
			free(boundaries[dim]);
	free(boundaries);
	free(dim_sizes);
	return false;
}

////////////////////////////////////////////////////////
// ResultTable operations 
////////////////////////////////////////////////////////

static hsize_t count_non_zero_values(double * array, hsize_t rank, hsize_t * width) {
	hsize_t count = 0;
	hsize_t pos;
	hsize_t max = volume(rank, width);
	for (pos = 0; pos < max; pos++) {
		if (array[pos])
			count++;
	}

	return count;
}

static hsize_t count_width_rank(hsize_t rank, bool * set_dims) {
	hsize_t dim;
	hsize_t count = 0;

	for (dim = 0; dim < rank; dim++)
		if (!set_dims[dim])
			count++;

	return count;
}

static hsize_t * projected_dims(hsize_t rank, hsize_t width_rank, bool * set_dims) {
	hsize_t dim;
	hsize_t pos = 0;
	hsize_t * res = calloc(width_rank, sizeof(hsize_t));

	for (dim = 0; dim < rank; dim++)
		if (!set_dims[dim])
			res[pos++] = dim;

	return res;
}

static void enter_new_data_point(ResultTable * table, hsize_t rank, hsize_t write_index, hsize_t * offset, double value) {
	table->values[write_index] = value;
	hsize_t * vector = calloc(rank, sizeof(hsize_t));
	hsize_t dim;
	for (dim = 0; dim < table->columns; dim++)
		vector[dim] = offset[table->dims[dim]];
	if (DEBUG) {
		printf("Found value %lf at (", value);
		for (dim = 0; dim < rank; dim++)
			printf("%lli,", offset[dim]);
		puts(")");
	}
	table->coords[write_index] = vector;
}

static void unroll_matrix_recursive(double ** reader_ptr, hsize_t rank, hsize_t* width, ResultTable * table, hsize_t * offset, hsize_t current_dim, hsize_t * write_index) {
	hsize_t index;
	if (current_dim == rank - 1) {
		for (index = 0; index < width[current_dim]; index++) {
			if (**reader_ptr) {
				enter_new_data_point(table, rank, *write_index, offset, **reader_ptr);
				(*write_index)++;
			}
			(*reader_ptr)++;
			offset[current_dim]++;
		}
	} else {
		for (index = 0; index < width[current_dim]; index++) {
			unroll_matrix_recursive(reader_ptr, rank, width, table, offset, current_dim + 1, write_index);
			offset[current_dim+1] = 0;
			offset[current_dim]++;
		}
	}
}

static ResultTable * newResultTable(double * array, hsize_t rank, hsize_t * offset, hsize_t* width, bool * set_dims) {
	ResultTable * table = calloc(1, sizeof(ResultTable));
	hsize_t width_rank = count_width_rank(rank, set_dims);
	table->columns = width_rank;
	table->dims = projected_dims(rank, width_rank, set_dims);
	table->rows = count_non_zero_values(array, rank, width);
	table->coords = calloc(table->rows, sizeof(hsize_t*));
	table->values = calloc(table->rows, sizeof(double));
	return table;
}

static ResultTable * unroll_matrix(double * array, hsize_t rank, hsize_t * offset, hsize_t* width, bool * set_dims) {
	ResultTable * table = newResultTable(array, rank, offset, width, set_dims);
	// We are about to start a recursion, defining a bunch of variables here
	hsize_t write_index = 0;
	hsize_t * offset2 = calloc(rank, sizeof(hsize_t));
	hsize_t dim;

	for (dim = 0; dim < rank; dim++)
		offset2[dim] = offset[dim];
	double * reader_ptr = array;
	unroll_matrix_recursive(&reader_ptr, rank, width, table, offset2, 0, &write_index);
	free(offset2);
	return table;
}

static void destroy_result_table(ResultTable * table) {
	int row;
	for (row = 0; row < table->rows; row++)
		free(table->coords[row]);
	free(table->coords);
	free(table->dims);
	free(table);
}

////////////////////////////////////////////////////////
// StringResultTable operations
////////////////////////////////////////////////////////

static char ** stringify_dim_names(hid_t file, ResultTable * table, StringArray * dim_names) {
	char ** res = calloc(table->columns, sizeof(char *));

	hsize_t dim;
	for (dim = 0; dim < table->columns; dim++) {
		res[dim] = get_string_in_array(dim_names, table->dims[dim]);
		if (DEBUG)
			printf("Converting dim %lli => %s\n", table->dims[dim], res[dim]);
	}
	
	if (res[0] == '\0')
		abort();

	return res;
}

static char *** stringify_coords(hid_t file, ResultTable * table, hsize_t * offset, StringArray ** dim_labels) {
	char *** coords = calloc(table->rows, sizeof(char **));
	hsize_t row, dim;
	for (row = 0; row < table->rows; row++) {
		coords[row] = calloc(table->columns, sizeof(char **));
		for (dim = 0; dim < table->columns; dim++) {
			coords[row][dim] = get_string_in_array(dim_labels[dim], table->coords[row][dim] - offset[table->dims[dim]]);
			if (DEBUG)
				printf("Converting dim %lli:%lli => %s\n", dim, table->coords[row][dim], coords[row][dim]);
	
			if (coords[row][dim] == '\0')
				abort();
		}
	}

	return coords;
}

static StringResultTable * stringify_result_table(hid_t file, hsize_t * offset, hsize_t * width, ResultTable * table) {
	StringResultTable * res = calloc(1, sizeof(StringResultTable));	
	res->rows = table->rows;
	res->columns = table->columns;
	res->dim_names = get_dim_names(file);
	res->dims = stringify_dim_names(file, table, res->dim_names);
	res->dim_labels = get_table_dims_labels(file, table, offset, width); 
	res->coords  = stringify_coords(file, table, offset, res->dim_labels);
	res->values = table->values;
	return res;
}

////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////

typedef struct dim_st {
	char * name;
	hsize_t size;
	char ** labels;
	hsize_t original;
} Dimension;

static int cmp_dims(const void * a, const void * b) {
	Dimension * A = (Dimension *) a;
	Dimension * B = (Dimension *) b;
	if (A->size != B->size)
		return A->size - B->size;
	else
		return A->original - B->original;
}

hid_t create_file(char * filename, hsize_t rank, char ** dim_names, hsize_t * dim_sizes, char *** dim_labels, hsize_t * chunk_sizes) {
	hsize_t core_rank = 0;
	hsize_t dim;
	Dimension * dims = calloc(rank, sizeof(Dimension));

	if (!chunk_sizes) {
		chunk_sizes = calloc(rank, sizeof(hsize_t));
		for (dim = 0; dim<rank; dim++) {
			if (dim_sizes[dim] < 100)
				chunk_sizes[dim] = dim_sizes[dim]; 
			else
				chunk_sizes[dim] = 100; 
		}
	}

	for (dim = 0; dim<rank; dim++) {
		if (dim_sizes[dim] > BIG_DIM_LENGTH)
			core_rank++;
		dims[dim].name = dim_names[dim];
		dims[dim].size = dim_sizes[dim];
		dims[dim].labels = dim_labels[dim];
		dims[dim].original = dim;
	}
	
	qsort(dims, rank, sizeof(Dimension), &cmp_dims);

	for (dim = 0; dim<rank; dim++) {
		dim_names[dim] = dims[dim].name;
		dim_sizes[dim] = dims[dim].size;
		dim_labels[dim] = dims[dim].labels;
	}
	
	hid_t file = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	store_dim_names(file, rank, dim_names);
	store_all_dims_labels(file, rank, dim_sizes, dim_labels);
	create_matrix(file, rank, dim_sizes, chunk_sizes);
	set_file_core_rank(file, core_rank);
	create_boundaries(file, rank, core_rank, dim_sizes);
	return file;
}

void store_values(hid_t file, hsize_t count, hsize_t ** coords, double * values) {
	if (DEBUG)
		printf("Storing %lli datapoints\n", count);
	store_values_in_matrix(file, count, coords, values);
	set_boundaries(file, count, coords);
}

hid_t open_file(char * filename) {
	return H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
}

StringResultTable * fetch_string_values(hid_t file, bool * set_dims, hsize_t * constraints) {
	hsize_t rank = get_file_rank(file);
	hsize_t * offset = calloc(rank, sizeof(hsize_t));
	hsize_t * width = calloc(rank, sizeof(hsize_t));

	if (set_query_parameters(file, rank, set_dims, constraints, offset, width))
		return NULL;

	if (DEBUG) {
		hid_t dim;
		printf("About to explore a field of (");
		for (dim = 0; dim < rank; dim++)
			printf("%llix", width[dim]);
		puts(") values;");
	}

	double * array = fetch_values(file, offset, width);

	if (DEBUG) {
		hid_t dim;
		printf("YAbout to explore a field of (");
		for (dim = 0; dim < rank; dim++)
			printf("%llix", width[dim]);
		puts(") values;");
	}

	ResultTable * table = unroll_matrix(array, rank, offset, width, set_dims);
	if (DEBUG) 
		printf("Found %lli values\n", table->rows);
	free(array);
	StringResultTable * res = stringify_result_table(file, offset, width, table);
	destroy_result_table(table);
	if (DEBUG) 
		printf("Returned %lli values\n", res->rows);
	return res;
} 

void destroy_string_result_table(StringResultTable * table) {
	hsize_t row;
	for (row = 0; row < table->rows; row++)
		free(table->coords[row]);
	hsize_t column;
	for (column = 0; column < table->columns; column++)
		destroy_string_array(table->dim_labels[column]);
	destroy_string_array(table->dim_names);
	free(table->dims);
	free(table->values);
	free(table);
}

void close_file(hid_t file) {
	H5Fclose(file);
}

////////////////////////////////////////////
// Testing functions
////////////////////////////////////////////

void set_big_dim_length(int length) {
	BIG_DIM_LENGTH = length;
}

void set_hdf5_log(bool value) {
	DEBUG = value;
}
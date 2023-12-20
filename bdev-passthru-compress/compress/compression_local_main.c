#include <time.h>

#include <pack.h>
#include <utils.h>
#include <doca_log.h>
#include <doca_ctx.h>
#include <doca_compress.h>
#include <samples/common.h>

#include "compression_local_core.h"

DOCA_LOG_REGISTER(COMPRESSION_LOCAL::Main);

struct doca_compress *compress_ctx = NULL;
struct program_core_objects state = {0};
uint64_t checksum;
uint64_t original_checksum;

void file_compress() {
	doca_compress_init(&compress_ctx, &state);
	doca_compress(&state, compress_ctx, &checksum);
	doca_compress_cleanup(&state, compress_ctx);
}

void file_decompress() {
	doca_compress_init(&compress_ctx, &state);
	doca_decompress(&state, compress_ctx, checksum);
	doca_compress_cleanup(&state, compress_ctx);
}

int main(int argc, char *argv[]) {
	char input_file_path[] = "input_files";
	char input_comp_file_path[] = "input_file_comp";
	char input_decomp_file_path[] = "input_file_decomp";
	int compress_method = COMPRESS_DEFLATE_SW;
	int decompress_method = COMPRESS_DEFLATE_HW;
	char *file_data = NULL;
	size_t file_len;
	uint8_t *compressed_file;
	size_t compressed_file_len;
	uint8_t *decompressed_file;
	size_t decompressed_file_len;
	doca_error_t result;

	if (argc > 1) {
		if (strcmp(argv[1], "hw") == 0) {
			compress_method = COMPRESS_DEFLATE_HW;
		} else if (strcmp(argv[1], "sw") == 0) {
			compress_method = COMPRESS_DEFLATE_SW;
		} else {
			DOCA_LOG_ERR("Invalid argument %s", argv[1]);
			return -1;
		}
	}
	if (argc > 2) {
		if (strcmp(argv[2], "hw") == 0) {
			decompress_method = COMPRESS_DEFLATE_HW;
		} else if (strcmp(argv[2], "sw") == 0) {
			decompress_method = COMPRESS_DEFLATE_SW;
		} else {
			DOCA_LOG_ERR("Invalid argument %s", argv[2]);
			return -1;
		}
	}

	/* Compression */
	read_local_file_naive(input_file_path, &file_data, &file_len);

	clock_t comp_start_time = clock();
	doca_compress_init(&compress_ctx, &state);
	result = compress_file(&state, file_data, file_len, DOCA_COMPRESS_DEFLATE_JOB, 
			compress_method, &compressed_file, &compressed_file_len, &original_checksum);
	
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to compress the received file");
		free(compressed_file);
		return -1;
	}
	doca_compress_cleanup(&state, compress_ctx);
	clock_t comp_end_time = clock();

	write_local_file(input_comp_file_path, compressed_file, compressed_file_len);
	
	double compress_time = (double)(comp_end_time - comp_start_time) / CLOCKS_PER_SEC;
	

	/* Decompression */
	read_local_file_naive(input_comp_file_path, (char **) &compressed_file, &compressed_file_len);
	
	clock_t decomp_start_time = clock();
	doca_compress_init(&compress_ctx, &state);
	result = compress_file(&state, (char *) compressed_file, compressed_file_len, DOCA_DECOMPRESS_DEFLATE_JOB, 
		decompress_method, &decompressed_file, &decompressed_file_len, &checksum);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decompress the received file");
		free(decompressed_file);
		return -1;
	}
	if (checksum != original_checksum) {
		DOCA_LOG_ERR("ERROR: file checksum is different. received: %ld, calculated: %ld", original_checksum, checksum);
		free(decompressed_file);
		return -1;
	}
	doca_compress_cleanup(&state, compress_ctx);
	clock_t decomp_end_time = clock();

	write_local_file(input_decomp_file_path, decompressed_file, decompressed_file_len);

	double decompress_time = (double)(decomp_end_time - decomp_start_time) / CLOCKS_PER_SEC;

	printf("Compression total time %f seconds. Decompression total time %f seconds.\n", compress_time, decompress_time);

	return EXIT_SUCCESS;
}

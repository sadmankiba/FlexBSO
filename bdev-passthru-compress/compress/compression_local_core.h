#ifndef COMPRESSION_LOCAL_H
#define COMPRESSION_LOCAL_H

/* File compression compress method */
enum file_compression_compress_method {
	COMPRESS_DEFLATE_HW,	/* Compress file using DOCA Compress library */
	COMPRESS_DEFLATE_SW	/* Compress file using zlib */
};

int doca_compress_init(struct doca_compress **compress_ctx, struct program_core_objects *state);
void doca_compress_cleanup(struct program_core_objects *state, struct doca_compress *compress_ctx);

int doca_compress(struct program_core_objects *state, struct doca_compress *compress_ctx, uint64_t *checksum);
int doca_decompress(struct program_core_objects *state, struct doca_compress *compress_ctx, uint64_t checksum);

doca_error_t read_local_file_mmap(char *file_path, char **file_data, size_t *file_len);
doca_error_t read_local_file_naive(char *file_path, char **file_data, size_t *file_len);
doca_error_t write_local_file(char *file_path, uint8_t *file_data, size_t file_len);

doca_error_t compress_file(struct program_core_objects *state, 
    char *file_data, size_t file_size, enum doca_compress_job_types job_type,
	enum file_compression_compress_method compress_method, uint8_t **compressed_file, 
    size_t *compressed_file_len, uint64_t *output_chksum);

#endif // COMPRESSION_LOCAL_H

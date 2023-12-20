#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <pack.h>
#include <utils.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_compress.h>

#include <samples/common.h>

#define MAX_FILE_NAME 255			/* Max file name */
#define SLEEP_IN_NANOS (10 * 1000)		/* Sample the job every 10 microseconds */
#define MAX_FILE_SIZE (1024 * 1024 * 1024)	/* 128 Mbytes, original 256 MB */
#define DECOMPRESS_RATIO 5			/* Maximal decompress ratio size, original 1032 */

DOCA_LOG_REGISTER(COMPRESSION_LOCAL::Core);

/* File compression compress method */
enum file_compression_compress_method {
	COMPRESS_DEFLATE_HW,	/* Compress file using DOCA Compress library */
	COMPRESS_DEFLATE_SW	/* Compress file using zlib */
};

/* File compression configuration struct */
struct file_compression_config {
	char file_path[MAX_FILE_NAME];				  /* Input file path */
	char output_file_path[MAX_FILE_NAME];			  /* Output file path */
	enum file_compression_compress_method compress_method;	  /* Whether to run compress with HW or SW */
};

/*
 * Unmap callback - free doca_buf allocated pointer
 *
 * @addr [in]: Memory range pointer
 * @len [in]: Memory range length
 * @opaque [in]: An opaque pointer passed to iterator
 */
static void
unmap_cb(void *addr, size_t len, void *opaque)
{
	(void)opaque;

	if (addr != NULL)
		munmap(addr, len);
}

/*
 * Submit compress job and retrieve the result
 *
 * @state [in]: application configuration struct
 * @job [in]: job to submit
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
process_job(struct program_core_objects *state, const struct doca_job *job)
{
	struct doca_event event = {0};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;

	/* Enqueue job */
	result = doca_workq_submit(state->workq, job);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit doca job: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state->workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve job: %s", doca_get_error_string(result));
	else if (event.result.u64 != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Job finished unsuccessfully");
		result = event.result.u64;
	} else
		result = DOCA_SUCCESS;

	return result;
}

/*
 * Populate destination doca buffer for compress jobs
 *
 * @state [in]: application configuration struct
 * @dst_buffer [in]: destination buffer
 * @dst_buf_size [in]: destination buffer size
 * @dst_doca_buf [out]: created doca buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
populate_dst_buf(struct program_core_objects *state, uint8_t **dst_buffer, size_t dst_buf_size, struct doca_buf **dst_doca_buf)
{
	doca_error_t result;

	dst_buffer = calloc(1, dst_buf_size);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, dst_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}

	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}

	result = doca_buf_inventory_buf_by_addr(state->buf_inv, state->dst_mmap, dst_buffer, dst_buf_size,
						dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s",
			     doca_get_error_string(result));
		return result;
	}
	return result;
}

/*
 * Construct compress job and submit it
 *
 * @state [in]: application configuration struct
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @job_type [in]: compress job type - compress or decompress
 * @dst_buf_size [in]: allocated destination buffer length
 * @compressed_file [out]: destination buffer with the result
 * @compressed_file_len [out]: destination buffer size
 * @output_chksum [out]: the returned checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
compress_file_hw(struct program_core_objects *state, char *file_data, size_t file_size,
	      enum doca_compress_job_types job_type, size_t dst_buf_size, uint8_t **compressed_file,
	      size_t *compressed_file_len, uint64_t *output_chksum)
{
	struct doca_buf *dst_doca_buf;
	struct doca_buf *src_doca_buf;
	uint8_t *resp_head;
	doca_error_t result;

	DOCA_LOG_INFO("(De-)compressing file with DOCA in hw. job type: %s", job_type == DOCA_COMPRESS_DEFLATE_JOB ? "compress" : "decompress");

	result = doca_mmap_set_memrange(state->src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range of source memory map: %s", doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_set_free_cb(state->src_mmap, &unmap_cb, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set free callback of source memory map: %s", doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start source memory map: %s", doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}

	result = doca_buf_inventory_buf_by_addr(state->buf_inv, state->src_mmap, file_data, file_size, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_get_error_string(result));
		return result;
	}

	doca_buf_get_data(src_doca_buf, (void **)&resp_head);
	doca_buf_set_data(src_doca_buf, resp_head, file_size);

	result = populate_dst_buf(state, compressed_file, dst_buf_size, &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}

	/* Construct compress job */
	const struct doca_compress_deflate_job compress_job = {
		.base = (struct doca_job) {
			.type = job_type,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state->ctx,
			},
		.dst_buff = dst_doca_buf,
		.src_buff = src_doca_buf,
		.output_chksum = output_chksum,
	};

	result = process_job(state, &compress_job.base);
	if (result != DOCA_SUCCESS) {
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}

	doca_buf_refcount_rm(src_doca_buf, NULL);

	doca_buf_get_head(dst_doca_buf, (void **)compressed_file);
	doca_buf_get_data_len(dst_doca_buf, compressed_file_len);
	doca_buf_refcount_rm(dst_doca_buf, NULL);

	DOCA_LOG_INFO("(De-)compressed file size in hw: %ld", *compressed_file_len);

	return DOCA_SUCCESS;
}

/*
 * Compress the input file in SW
 *
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @dst_buf_size [in]: allocated destination buffer length
 * @compressed_file [out]: destination buffer with the result
 * @compressed_file_len [out]: destination buffer size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
compress_file_sw(char *file_data, size_t file_size, size_t dst_buf_size, Byte **compressed_file, uLong *compressed_file_len)
{
	z_stream c_stream; /* compression stream */
	int err;

	DOCA_LOG_INFO("Compressing file with zlib in sw.");

	memset(&c_stream, 0, sizeof(c_stream));

	c_stream.zalloc = NULL;
	c_stream.zfree = NULL;

	err = deflateInit2(&c_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	c_stream.next_in  = (z_const unsigned char *)file_data;
	c_stream.next_out = *compressed_file;

	c_stream.avail_in = file_size;
	c_stream.avail_out = dst_buf_size;
	err = deflate(&c_stream, Z_NO_FLUSH);
	if (err < 0) {
		DOCA_LOG_ERR("Failed to compress file. Deflate z_no_flush err: %d", err);
		return DOCA_ERROR_BAD_STATE;
	}

	/* Finish the stream */
	err = deflate(&c_stream, Z_FINISH);
	if (err < 0 || err != Z_STREAM_END) {
		DOCA_LOG_ERR("Failed to compress file. Deflate z_finish err: %d", err);
		return DOCA_ERROR_BAD_STATE;
	}

	err = deflateEnd(&c_stream);
	if (err < 0) {
		DOCA_LOG_ERR("Failed to compress file. Deflate end err: %d", err);
		return DOCA_ERROR_BAD_STATE;
	}
	*compressed_file_len = c_stream.total_out;

	DOCA_LOG_INFO("Compressed file size in sw: %ld", *compressed_file_len);
	return DOCA_SUCCESS;
}

/*
 * Calculate file checksum with zlib, where the lower 32 bits contain the CRC checksum result
 * and the upper 32 bits contain the Adler checksum result.
 *
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @output_chksum [out]: the calculated checksum
 */
static void
calculate_checksum_sw(char *file_data, size_t file_size, uint64_t *output_chksum)
{
	uint32_t crc;
	uint32_t adler;
	uint64_t result_checksum;

	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, (const unsigned char *)file_data, file_size);
	adler = adler32(0L, Z_NULL, 0);
	adler = adler32(adler, (const unsigned char *)file_data, file_size);

	result_checksum = adler;
	result_checksum <<= 32;
	result_checksum += crc;

	*output_chksum = result_checksum;
}

/*
 * Compress / decompress the input file data
 *
 * @state [in]: application configuration struct
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @job_type [in]: compress job type - compress for client and decompress for server
 * @compress_method [in]: compress with software or hardware
 * @compressed_file [out]: destination buffer with the result
 * @compressed_file_len [out]: destination buffer size
 * @output_chksum [out]: the calculated checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
compress_file(struct program_core_objects *state, char *file_data, size_t file_size, enum doca_compress_job_types job_type,
	      enum file_compression_compress_method compress_method, uint8_t **compressed_file, size_t *compressed_file_len,
	      uint64_t *output_chksum)
{	
	DOCA_LOG_INFO("In compress_file. file size %ld, job type %d, compress_method %d", file_size, job_type, compress_method);
	size_t dst_buf_size = 0;

	if (job_type == DOCA_COMPRESS_DEFLATE_JOB) {
		dst_buf_size = MAX(file_size + 16, file_size * 2);
		if (dst_buf_size > MAX_FILE_SIZE)
			dst_buf_size = MAX_FILE_SIZE;
	} else if (job_type == DOCA_DECOMPRESS_DEFLATE_JOB)
		dst_buf_size = MIN(MAX_FILE_SIZE, DECOMPRESS_RATIO * file_size);

	*compressed_file = calloc(1, dst_buf_size);
	if (*compressed_file == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}
	DOCA_LOG_INFO("Allocated dst buffer size: %ld", dst_buf_size);

	if (compress_method == COMPRESS_DEFLATE_SW) {
		calculate_checksum_sw(file_data, file_size, output_chksum);
		return compress_file_sw(file_data, file_size, dst_buf_size, compressed_file, compressed_file_len);
	} else
		return compress_file_hw(state, file_data, file_size, job_type, dst_buf_size, compressed_file, compressed_file_len, output_chksum);
}

doca_error_t
read_local_file(char *file_path, char **file_data, size_t *file_len) {
	int fd;

	fd = open(file_path, O_RDWR);
	struct stat statbuf;

	if (fd < 0) {
		DOCA_LOG_ERR("Failed to open %s", file_path);
		return DOCA_ERROR_IO_FAILED;
	}

	if (fstat(fd, &statbuf) < 0) {
		DOCA_LOG_ERR("Failed to get file information");
		close(fd);
		return DOCA_ERROR_IO_FAILED;
	}

	if (statbuf.st_size == 0 || statbuf.st_size > MAX_FILE_SIZE) {
		DOCA_LOG_ERR("Invalid file size. Should be greater then zero and smaller then two Gbytes");
		close(fd);
		return DOCA_ERROR_INVALID_VALUE;
	}

	*file_data = mmap(NULL, statbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (*file_data == MAP_FAILED) {
		DOCA_LOG_ERR("Unable to map file content: %s", strerror(errno));
		close(fd);
		return DOCA_ERROR_NO_MEMORY;
	}
	*file_len = statbuf.st_size;

	DOCA_LOG_INFO("File read. Size : %ld", *file_len);
	
	return DOCA_SUCCESS;
}

doca_error_t
write_local_file(char *file_path, uint8_t *file_data, size_t file_len) {
	int fd;
	doca_error_t result;

	fd = open(file_path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP);
	if (fd < 0) {
		DOCA_LOG_ERR("Failed to open %s", file_path);
		free(file_data);
		result = DOCA_ERROR_IO_FAILED;
		return result;
	}

	if ((size_t)write(fd, file_data, file_len) != file_len) {
		DOCA_LOG_ERR("Failed to write the file");
		free(file_data);
		close(fd);
		result = DOCA_ERROR_IO_FAILED;
		return result;
	}

	close(fd);

	DOCA_LOG_INFO("File written. Size : %ld", file_len);

	return DOCA_SUCCESS;
}

/*
 * Compress the input file data and write it to the output file
 */
doca_error_t
file_compressor(struct file_compression_config *app_cfg, struct program_core_objects *state, uint64_t *checksum)
{
	char *file_data = NULL;
	size_t file_len;
	uint8_t *compressed_file;
	size_t compressed_file_len;
	doca_error_t result;

	DOCA_LOG_INFO("Starting compression");

	read_local_file(app_cfg->file_path, &file_data, &file_len);

	/* Send compress job */

	clock_t start_time = clock();

	result = compress_file(state, file_data, file_len, DOCA_COMPRESS_DEFLATE_JOB, app_cfg->compress_method, &compressed_file,
					   &compressed_file_len, checksum);

	clock_t end_time = clock();
	double execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

	DOCA_LOG_INFO("Compressed file size: %ld", compressed_file_len);
	DOCA_LOG_INFO("Compression time: %f seconds", execution_time);


	if (result != DOCA_SUCCESS) {
		free(compressed_file);
		return result;
	}

	DOCA_LOG_INFO("File compressed");

	write_local_file(app_cfg->output_file_path, compressed_file, compressed_file_len);

	return result;
}

/*
 * Decompress the input file data and write it to the output file
 */
doca_error_t
file_decompressor(struct file_compression_config *app_cfg, struct program_core_objects *state, uint64_t original_checksum)
{
	char *compressed_file = NULL;
	size_t compressed_file_len;
	uint8_t *decompressed_file;
	uint64_t checksum;
	size_t decompressed_file_len;
	
	doca_error_t result;

	DOCA_LOG_INFO("Starting decompression");

	read_local_file(app_cfg->file_path, &compressed_file, &compressed_file_len);

	clock_t start_time = clock();

	result = compress_file(state, compressed_file, compressed_file_len, DOCA_DECOMPRESS_DEFLATE_JOB, 
		COMPRESS_DEFLATE_HW, &decompressed_file, &decompressed_file_len, &checksum);

	clock_t end_time = clock();
	double execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
	
	DOCA_LOG_INFO("Decompressed file size: %ld", decompressed_file_len);
	DOCA_LOG_INFO("Decompression time: %f seconds", execution_time);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decompress the received file");
		free(decompressed_file);
		goto finish;
	}
	if (checksum == original_checksum)
		DOCA_LOG_INFO("SUCCESS: file was received and decompressed successfully");
	else {
		DOCA_LOG_ERR("ERROR: file checksum is different. received: %ld, calculated: %ld", original_checksum, checksum);
		free(decompressed_file);
		result = DOCA_ERROR_BAD_STATE;
		goto finish;
	}

	DOCA_LOG_INFO("File decompressed");

	write_local_file(app_cfg->output_file_path, decompressed_file, decompressed_file_len);

	free(decompressed_file);

finish:
	return result;
}

/**
 * Check if given device is capable of executing both compress and decompress.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_DECOMPRESS_DEFLATE_JOB and DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_compress_decompress_is_supported(struct doca_devinfo *devinfo)
{
	return doca_compress_job_get_supported(devinfo, DOCA_COMPRESS_DEFLATE_JOB) && 
		doca_compress_job_get_supported(devinfo, DOCA_DECOMPRESS_DEFLATE_JOB);
}

doca_error_t
file_compression_init(struct file_compression_config *app_cfg, struct program_core_objects *state,
		struct doca_compress **compress_ctx)
{
	uint32_t workq_depth = 1; /* The app will run 1 compress job at a time */
	uint32_t max_bufs = 2;    /* The app will use 2 doca buffers */
	doca_error_t result;

	/* create compress library */
	result = doca_compress_create(compress_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init compress library: %s", doca_get_error_string(result));
		return result;
	}

	state->ctx = doca_compress_as_ctx(*compress_ctx);

	result = open_doca_device_with_capabilities(&compress_jobs_compress_decompress_is_supported, &state->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device for DOCA Compress: %s", doca_get_error_string(result));
		app_cfg->compress_method = COMPRESS_DEFLATE_HW;
		goto compress_destroy;
	}

	/* Core objects are initialized only for Hw */
	result = init_core_objects(state, workq_depth, max_bufs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA core objects: %s", doca_get_error_string(result));
		goto destroy_core_objs;
	}

	return DOCA_SUCCESS;

destroy_core_objs:
	if (app_cfg->compress_method == COMPRESS_DEFLATE_HW)
		destroy_core_objects(state);
compress_destroy:
	doca_compress_destroy(*compress_ctx);
	return result;
}

void
file_compression_cleanup(struct program_core_objects *state, struct file_compression_config *app_cfg,
			 struct doca_compress *compress_ctx)
{
	doca_error_t result;

	result = destroy_core_objects(state);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy core objects: %s", doca_get_error_string(result));

	result = doca_compress_destroy(compress_ctx);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy compress: %s", doca_get_error_string(result));
}

int main() {
	struct file_compression_config app_cfg = {};
	struct doca_compress *compress_ctx = NULL;
	struct program_core_objects state = {0};
	uint64_t checksum;
	doca_error_t result;

	clock_t init_start_time = clock();

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

    /* Init compress library */
	
	result = file_compression_init(&app_cfg, &state, &compress_ctx);

	clock_t init_end_time = clock();
	double init_execution_time = (double)(init_end_time - init_start_time) / CLOCKS_PER_SEC;
	
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init compress library: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	
	/* Compress a file in sw */
	strlcpy(app_cfg.file_path, "input.txt", MAX_FILE_NAME);
	strlcpy(app_cfg.output_file_path, "input-comp.txt", MAX_FILE_NAME);
	app_cfg.compress_method = COMPRESS_DEFLATE_HW;

	result = file_compressor(&app_cfg, &state, &checksum);
    
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("File compression encountered errors");
		file_compression_cleanup(&state, &app_cfg, compress_ctx);
		return EXIT_FAILURE;
	}

	/* Decompress a file in hw */
	if (app_cfg.compress_method == COMPRESS_DEFLATE_HW) {
		file_compression_cleanup(&state, &app_cfg, compress_ctx);
		result = file_compression_init(&app_cfg, &state, &compress_ctx);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to init compress library: %s", doca_get_error_string(result));
			return EXIT_FAILURE;
		}
	}
	
	strlcpy(app_cfg.file_path, "input-comp.txt", MAX_FILE_NAME);
	strlcpy(app_cfg.output_file_path, "input-decomp.txt", MAX_FILE_NAME);
	app_cfg.compress_method = COMPRESS_DEFLATE_HW;

	result = file_decompressor(&app_cfg, &state, checksum);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("File decompression encountered errors");
		file_compression_cleanup(&state, &app_cfg, compress_ctx);
		return EXIT_FAILURE;
	}

	/* Cleanup */
	clock_t cleanup_start_time = clock();
	file_compression_cleanup(&state, &app_cfg, compress_ctx);
	clock_t cleanup_end_time = clock();
	double cleanup_execution_time = (double)(cleanup_end_time - cleanup_start_time) / CLOCKS_PER_SEC;

	DOCA_LOG_INFO("Init time: %f seconds. Cleanup time %f seconds", init_execution_time, cleanup_execution_time);
	return EXIT_SUCCESS;
}
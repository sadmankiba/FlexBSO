/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   All rights reserved.
 */

#include <time.h>

#include "spdk/stdinc.h"
#include "spdk/thread.h"
#include "spdk/bdev.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/bdev_zone.h"
#include "spdk/bdev_module.h"

#define MAX_BLOCK_SIZE (128 * 1024) /* 128 KB */

#define PROG_DEBUG 0

static char *g_bdev_name = "Malloc0";

/*
 * We'll use this struct to gather housekeeping hello_context to pass between
 * our events and callbacks.
 */
struct hello_context_t {
  struct spdk_bdev *bdev;
  struct spdk_bdev_desc *bdev_desc;
  struct spdk_io_channel *bdev_io_channel;
  char *buff;
  uint32_t buff_size;
  char *bdev_name;
  struct spdk_bdev_io_wait_entry bdev_io_wait;
};

enum IO_WORKLOAD {
  STRING,
  MULTI_BLOCK,
  LARGE_FILE
};
int workload = LARGE_FILE;

/*
* For large file workload,
* we obtain two large buffers with spdk_dma_zmalloc() 
* at beginning for input and output data.
* Pointers from these buffers are used to write and read data from the bdev.
*/
char input_file_name[] = "input_file";
char output_file_name[] = "output_file";
char *input_data;
char *output_data;
uint64_t file_in_size;
uint32_t file_write_offset = 0; /* Increased on every write complete */
uint32_t file_read_offset = 0; /* Increased on every read complete */
uint32_t bdev_write_offset = 0; /* Increased on every bdev write submitted */
uint32_t bdev_read_offset = 0; /* Increased on every bdev read submitted */
clock_t write_start_time;
clock_t write_end_time;
clock_t read_start_time;
clock_t read_end_time;
clock_t spdk_init_start_time;
clock_t spdk_init_end_time;
clock_t spdk_fini_start_time;
clock_t spdk_fini_end_time;

static void hello_read(void *arg);
static void hello_write(void *arg);

/*
 * Usage function for printing parameters that are specific to this application
 */
static void
hello_bdev_usage(void)
{
  printf(" -b <bdev>                 name of the bdev to use\n");
}

/*
 * This function is called to parse the parameters that are specific to this application
 */
static int
hello_bdev_parse_arg(int ch, char *arg)
{
  switch (ch) {
  case 'b':
    g_bdev_name = arg;
    break;
  default:
    return -EINVAL;
  }
  return 0;
}

/*
 * Callback function for read io completion.
 */
static void
read_complete(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
  struct hello_context_t *hello_context = cb_arg;

  if (PROG_DEBUG) {
    SPDK_NOTICELOG("bdev io %p read completed\n", bdev_io);
    
    char temp[6];
    temp[5] = '\0';
    for (int i = 0; i < 5; i++) {
      temp[i] = ((char *)bdev_io->u.bdev.iovs[0].iov_base)[i];
    }
    if (success) {
      SPDK_NOTICELOG("Read string from bdev (first 5 chars): %s\n", temp);
    } else {
      SPDK_ERRLOG("bdev io read error\n");
    }
  }
  
  if (workload == LARGE_FILE) {   
    file_read_offset += hello_context->buff_size; 
    
    if (file_read_offset >= file_in_size)  {
      /* At this point, all read is done */
      
      read_end_time = clock();
      spdk_bdev_free_io(bdev_io);

      /* Write data */
      FILE *file_out = fopen(output_file_name, "w");
      if (file_out == NULL) {
        printf("Failed to open the file.\n");
        spdk_app_stop(-1);
        return;
      }
      uint32_t written = 0;
      while(written < file_in_size) {
        fwrite(output_data + written, 1, hello_context->buff_size, file_out);
        written += hello_context->buff_size;
      }
      fclose(file_out);
      
      /* Close the channel */
      spdk_fini_start_time = clock();
      spdk_put_io_channel(hello_context->bdev_io_channel);
      spdk_bdev_close(hello_context->bdev_desc);
      SPDK_NOTICELOG("Stopping app\n");
      spdk_app_stop(0);
      return;
    }
    
  } else {
    /* Complete the bdev io and close the channel */
    read_end_time = clock();
    spdk_bdev_free_io(bdev_io);
    spdk_put_io_channel(hello_context->bdev_io_channel);
    spdk_bdev_close(hello_context->bdev_desc);
    SPDK_NOTICELOG("Stopping app\n");
    spdk_app_stop(success ? 0 : -1);
  }
}

static void
hello_read(void *arg)
{
  struct hello_context_t *hello_context = arg;
  int rc = 0;
  uint64_t offset;
  
  read_start_time = clock();
  if (workload == LARGE_FILE) {
    while(bdev_read_offset < file_in_size) {
      rc = spdk_bdev_read(hello_context->bdev_desc, hello_context->bdev_io_channel,
            hello_context->buff, bdev_read_offset, hello_context->buff_size, read_complete,
            hello_context);
      bdev_read_offset += hello_context->buff_size;
      hello_context->buff = output_data + bdev_read_offset;
    }
  } else {
    offset = 0;
    rc = spdk_bdev_read(hello_context->bdev_desc, hello_context->bdev_io_channel,
		      hello_context->buff, offset, hello_context->buff_size, read_complete,
		      hello_context);
  }

  if (rc == -ENOMEM) {
    SPDK_NOTICELOG("Queueing io\n");
    /* In case we cannot perform I/O now, queue I/O */
    hello_context->bdev_io_wait.bdev = hello_context->bdev;
    hello_context->bdev_io_wait.cb_fn = hello_read;
    hello_context->bdev_io_wait.cb_arg = hello_context;
    spdk_bdev_queue_io_wait(hello_context->bdev, hello_context->bdev_io_channel,
			    &hello_context->bdev_io_wait);
  } else if (rc) {
    SPDK_ERRLOG("%s error while reading from bdev: %d\n", spdk_strerror(-rc), rc);
    spdk_put_io_channel(hello_context->bdev_io_channel);
    spdk_bdev_close(hello_context->bdev_desc);
    spdk_app_stop(-1);
  }
}

/*
 * Callback function for write io completion.
 */
static void
write_complete(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
  struct hello_context_t *hello_context = cb_arg;

  /* Complete the I/O */

  if(PROG_DEBUG) {
    SPDK_NOTICELOG("bdev io %p write completed\n", bdev_io);
  }

  char temp[6];
  if (success) {
    if (PROG_DEBUG) {
      memcpy(temp, bdev_io, 5);
      temp[5] = '\0';
      
      SPDK_NOTICELOG("Wrote (first 5 chars): %s\n", temp);
    }
  } else {
    SPDK_ERRLOG("bdev io write error: %d\n", EIO);
    spdk_put_io_channel(hello_context->bdev_io_channel);
    spdk_bdev_close(hello_context->bdev_desc);
    spdk_app_stop(-1);
    return;
  }

  spdk_bdev_free_io(bdev_io);

  if (workload == LARGE_FILE) {
    file_write_offset += hello_context->buff_size;
    
    if (file_write_offset >= file_in_size) {
      /* At this point, all write is done */
      write_end_time = clock();
      hello_context->buff = output_data;
      hello_read(hello_context);
    }
  } else {
    /* Zero the buffer so that we can use it for reading */
    write_end_time = clock();
    memset(hello_context->buff, 0, hello_context->buff_size);
    hello_read(hello_context);
  }
}

static void
hello_write(void *arg)
{
  struct hello_context_t *hello_context = arg;
  int rc = 0;
  uint64_t offset;
  
  if (PROG_DEBUG) {
    SPDK_NOTICELOG("Writing to the bdev\n");
  }

  write_start_time = clock();

  if (workload == LARGE_FILE) {
    while(bdev_write_offset < file_in_size) {
      rc = spdk_bdev_write(hello_context->bdev_desc, hello_context->bdev_io_channel,
            hello_context->buff, bdev_write_offset, hello_context->buff_size, write_complete,
            hello_context);
      bdev_write_offset += hello_context->buff_size;
      hello_context->buff = input_data + bdev_write_offset; 
    }
  } else {
    offset = 0;
    rc = spdk_bdev_write(hello_context->bdev_desc, hello_context->bdev_io_channel,
		       hello_context->buff, offset, hello_context->buff_size, write_complete,
		       hello_context);
  }

  if (rc == -ENOMEM) {
    SPDK_NOTICELOG("Queueing io\n");
    /* In case we cannot perform I/O now, queue I/O */
    hello_context->bdev_io_wait.bdev = hello_context->bdev;
    hello_context->bdev_io_wait.cb_fn = hello_write;
    hello_context->bdev_io_wait.cb_arg = hello_context;
    spdk_bdev_queue_io_wait(hello_context->bdev, hello_context->bdev_io_channel,
			    &hello_context->bdev_io_wait);
  } else if (rc) {
    SPDK_ERRLOG("%s error while writing to bdev: %d\n", spdk_strerror(-rc), rc);
    spdk_put_io_channel(hello_context->bdev_io_channel);
    spdk_bdev_close(hello_context->bdev_desc);
    spdk_app_stop(-1);
  }
}

static void
hello_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		    void *event_ctx)
{
  SPDK_NOTICELOG("Unsupported bdev event: type %d\n", type);
}

static void
reset_zone_complete(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
  struct hello_context_t *hello_context = cb_arg;

  /* Complete the I/O */
  spdk_bdev_free_io(bdev_io);

  if (!success) {
    SPDK_ERRLOG("bdev io reset zone error: %d\n", EIO);
    spdk_put_io_channel(hello_context->bdev_io_channel);
    spdk_bdev_close(hello_context->bdev_desc);
    spdk_app_stop(-1);
    return;
  }

  hello_write(hello_context);
}

static void
hello_reset_zone(void *arg)
{
  struct hello_context_t *hello_context = arg;
  int rc = 0;

  rc = spdk_bdev_zone_management(hello_context->bdev_desc, hello_context->bdev_io_channel,
				 0, SPDK_BDEV_ZONE_RESET, reset_zone_complete, hello_context);

  if (rc == -ENOMEM) {
    SPDK_NOTICELOG("Queueing io\n");
    /* In case we cannot perform I/O now, queue I/O */
    hello_context->bdev_io_wait.bdev = hello_context->bdev;
    hello_context->bdev_io_wait.cb_fn = hello_reset_zone;
    hello_context->bdev_io_wait.cb_arg = hello_context;
    spdk_bdev_queue_io_wait(hello_context->bdev, hello_context->bdev_io_channel,
			    &hello_context->bdev_io_wait);
  } else if (rc) {
    SPDK_ERRLOG("%s error while resetting zone: %d\n", spdk_strerror(-rc), rc);
    spdk_put_io_channel(hello_context->bdev_io_channel);
    spdk_bdev_close(hello_context->bdev_desc);
    spdk_app_stop(-1);
  }
}

/*
 * Generate a string with many consecutive repeated characters.
*/
int gen_string(char *buf, uint32_t size) {
  uint32_t i;
  uint32_t num_chars = 10;
  for (i = 0; i < size; i++) {
    buf[i] = 'a' + ((uint32_t) (i * num_chars / size));
  }

  buf[size - 1] = '\0';
  
  return 0;
} 

/*
 * Our initial event that kicks off everything from main().
 */
static void
hello_start(void *arg1)
{
  struct hello_context_t *hello_context = arg1;
  uint32_t buf_align;
  int rc = 0;
  uint32_t buf_size_factor;
  hello_context->bdev = NULL;
  hello_context->bdev_desc = NULL;
  size_t input_file_offset = 0;

  SPDK_NOTICELOG("Successfully started the application\n");

  /*
   * There can be many bdevs configured, but this application will only use
   * the one input by the user at runtime.
   *
   * Open the bdev by calling spdk_bdev_open_ext() with its name.
   * The function will return a descriptor
   */
  SPDK_NOTICELOG("Opening the bdev %s\n", hello_context->bdev_name);
  rc = spdk_bdev_open_ext(hello_context->bdev_name, true, hello_bdev_event_cb, NULL,
			  &hello_context->bdev_desc);
  if (rc) {
    SPDK_ERRLOG("Could not open bdev: %s\n", hello_context->bdev_name);
    spdk_app_stop(-1);
    return;
  }

  /* A bdev pointer is valid while the bdev is opened. */
  hello_context->bdev = spdk_bdev_desc_get_bdev(hello_context->bdev_desc);
  spdk_init_end_time = clock();

  if (workload == LARGE_FILE) {
    buf_align = spdk_bdev_get_buf_align(hello_context->bdev);
    input_data = spdk_dma_zmalloc(256 * 1024 * 1024, buf_align, NULL); // 256 MB
    
    buf_align = spdk_bdev_get_buf_align(hello_context->bdev);
    output_data = spdk_dma_zmalloc(256 * 1024 * 1024, buf_align, NULL); // 256 MB
    SPDK_NOTICELOG("Allocated input data at %p, and output buffer at %p\n", input_data, output_data);

    /* Open file */
    FILE *file_in = fopen(input_file_name, "r");
    if (file_in == NULL) {
      printf("Failed to open the file.\n");
      spdk_app_stop(-1);
      return;
    }

    /* Read file */
    while(1) {
      size_t bytesRead = fread(input_data + input_file_offset, 1, 1024 * 1024, file_in);
      if (bytesRead == 0) {
        break;
      }
      input_file_offset += bytesRead;
    }
    fclose(file_in);
    file_in_size = input_file_offset;
    SPDK_NOTICELOG("Input file size: %zu B.\n", file_in_size);

    // memset(output_data, 0, 1024 * 1024 * 1024);
  }

  SPDK_NOTICELOG("Opening io channel\n");
  /* Open I/O channel */
  hello_context->bdev_io_channel = spdk_bdev_get_io_channel(hello_context->bdev_desc);
  if (hello_context->bdev_io_channel == NULL) {
    SPDK_ERRLOG("Could not create bdev I/O channel!!\n");
    spdk_bdev_close(hello_context->bdev_desc);
    spdk_app_stop(-1);
    return;
  }
  
  hello_context->buff_size = spdk_bdev_get_block_size(hello_context->bdev) *
    spdk_bdev_get_write_unit_size(hello_context->bdev);

  switch (workload) {
    case MULTI_BLOCK:
      hello_context->buff_size *= 8;
      break;
    case LARGE_FILE:
      hello_context->buff_size = MAX_BLOCK_SIZE;
      break;
  }

  SPDK_NOTICELOG("Allocating buffer of size %u bytes\n", hello_context->buff_size);

  /* Allocate memory for the write buffer.
   * Initialize the write buffer with a string
   */
  if (workload != LARGE_FILE) {
    buf_align = spdk_bdev_get_buf_align(hello_context->bdev);
    hello_context->buff = spdk_dma_zmalloc(hello_context->buff_size, buf_align, NULL);
    if (!hello_context->buff) {
      SPDK_ERRLOG("Failed to allocate buffer\n");
      spdk_put_io_channel(hello_context->bdev_io_channel);
      spdk_bdev_close(hello_context->bdev_desc);
      spdk_app_stop(-1);
      return;
    }
  }
    
  
  switch (workload) {
    case STRING:
      snprintf(hello_context->buff, hello_context->buff_size, "%s", "Hello World!\n");
      break;
    case MULTI_BLOCK:
      gen_string(hello_context->buff, hello_context->buff_size);
      break;
    case LARGE_FILE:
      hello_context->buff = input_data;
      break;
  }

  hello_write(hello_context);
}

int
main(int argc, char **argv)
{
  struct spdk_app_opts opts = {};
  int rc = 0;
  struct hello_context_t hello_context = {};
  double write_cpu_time;
  double read_cpu_time;
  double spdk_init_cpu_time;
  double spdk_fini_cpu_time;

  spdk_init_start_time = clock();
  /* Set default values in opts structure. */
  spdk_app_opts_init(&opts, sizeof(opts));
  opts.name = "hello_bdev";

  /*
   * Parse built-in SPDK command line parameters as well
   * as our custom one(s).
   */
  if ((rc = spdk_app_parse_args(argc, argv, &opts, "b:", NULL, hello_bdev_parse_arg,
				hello_bdev_usage)) != SPDK_APP_PARSE_ARGS_SUCCESS) {
    exit(rc);
  }
  hello_context.bdev_name = g_bdev_name;

  /*
   * spdk_app_start() will initialize the SPDK framework, call hello_start(),
   * and then block until spdk_app_stop() is called (or if an initialization
   * error occurs, spdk_app_start() will return with rc even without calling
   * hello_start().
   */
  rc = spdk_app_start(&opts, hello_start, &hello_context);
  if (rc) {
    SPDK_ERRLOG("ERROR starting application\n");
  }

  /* At this point either spdk_app_stop() was called, or spdk_app_start()
   * failed because of internal error.
   */
  spdk_fini_end_time = clock();

  write_cpu_time = ((double) (write_end_time - write_start_time)) / CLOCKS_PER_SEC;
  read_cpu_time = ((double) (read_end_time - read_start_time)) / CLOCKS_PER_SEC;
  spdk_init_cpu_time = ((double) (spdk_init_end_time - spdk_init_start_time)) / CLOCKS_PER_SEC;
  spdk_fini_cpu_time = ((double) (spdk_fini_end_time - spdk_fini_start_time)) / CLOCKS_PER_SEC;

  SPDK_NOTICELOG("Time: write %fs, read %fs, init %fs, finish %fs\n", write_cpu_time, read_cpu_time, spdk_init_cpu_time, spdk_fini_cpu_time);

  /* When the app stops, free up memory that we allocated. */
  if (workload == LARGE_FILE) {
    spdk_dma_free(input_data);
    spdk_dma_free(output_data);
  } else {
    spdk_dma_free(hello_context.buff);
  }

  /* Gracefully close out all of the SPDK subsystems. */
  SPDK_NOTICELOG("Finishing app\n");
  spdk_app_fini();
  return rc;
}

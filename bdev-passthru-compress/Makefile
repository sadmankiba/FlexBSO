#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2020 Intel Corporation.
#  All rights reserved.
#

export SPDK_DIR=/opt/mellanox/spdk
export SPDK_HEADER_DIR=$(SPDK_DIR)/include
export SPDK_LIB_DIR=$(SPDK_DIR)/lib

ALL_DEPDIRS+= hello_world passthru compress

COMMON_CFLAGS+=-I$(SPDK_HEADER_DIR)
COMMON_CFLAGS+=-L$(SPDK_LIB_DIR)

export
.PHONY: all

all: hello_world_bdev_shared_combo_compress

static: hello_world_bdev_static

hello_world_bdev_shared_combo_compress: passthru_shared_compress
	$(MAKE) --directory=hello_world bdev_shared_combo

hello_world_bdev_shared_combo: passthru_shared
	$(MAKE) --directory=hello_world bdev_shared_combo

hello_world_bdev_shared_iso: passthru_shared
	$(MAKE) --directory=hello_world bdev_shared_iso

hello_world_no_bdev_shared_combo:
	$(MAKE) --directory=hello_world alone_shared_combo

hello_world_no_bdev_shared_iso:
	$(MAKE) --directory=hello_world alone_shared_iso

hello_world_bdev_static: passthru_static
	$(MAKE) --directory=hello_world bdev_static

hello_world_no_bdev_static:
	$(MAKE) --directory=hello_world alone_static

passthru_shared_compress: compress_app
	$(MAKE) --directory=passthru shared

passthru_shared:
	$(MAKE) --directory=passthru shared

passthru_static:
	$(MAKE) --directory=passthru static

compress_app: 
	$(MAKE) --directory=compress -f Makefile-single

clean:
	rm -f ./hello_world/hello_bdev
	rm -f ./passthru/libpassthru_external.*
	rm -rf ./compress/doca_compression_local.p/
	rm -f ./compress/doca_compression_local


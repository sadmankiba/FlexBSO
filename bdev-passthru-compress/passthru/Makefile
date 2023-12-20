#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2020 Intel Corporation.
#  All rights reserved.
#

src=vbdev_passthru_rpc.c vbdev_passthru.c

DOCA_PATH = /opt/mellanox/doca
DOCA_APP_PATH = $(DOCA_PATH)/applications
DOCA_COMMON_PATH = $(DOCA_APP_PATH)/common/src
DOCA_INCLUDE_PATH = $(DOCA_PATH)/include

DOCA_OBJ_FILES = ../compress/doca_compression_local.p/compression_local_core.c.o ../compress/doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o

DOCA_LINK_ARGS = -Wl,-rpath,/opt/mellanox/doca/lib/aarch64-linux-gnu \
	-Wl,-rpath-link,/opt/mellanox/doca/lib/aarch64-linux-gnu \
	-pthread -Wl,--start-group /usr/lib/aarch64-linux-gnu/libjson-c.so \
	/usr/lib/aarch64-linux-gnu/libbsd.so \
	-Wl,--as-needed /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_common.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_argp.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpdk_bridge.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dma.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_compress.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_telemetry.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_ipsec.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_flow.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_erasure_coding.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_flow_ct.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_comm_channel.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_regex.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_apsh.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_sha.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpa.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_rdma.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpi.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_eth.so \
	/opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_pcc.so \
	/usr/lib/aarch64-linux-gnu/libz.so -Wl,--end-group

shared:
	$(CC) $(COMMON_CFLAGS) -c -fPIC ./vbdev_passthru_rpc.c -o ./vbdev_passthru_rpc.o
	$(CC) $(COMMON_CFLAGS) -I$(DOCA_COMMON_PATH) -I$(DOCA_INCLUDE_PATH) -I$(DOCA_PATH) -I../compress  -c -fPIC ./vbdev_passthru.c -o ./vbdev_passthru.o
	$(CC) $(COMMON_CFLAGS) -shared ./vbdev_passthru_rpc.o ./vbdev_passthru.o $(DOCA_OBJ_FILES) -o ./libpassthru_external.so $(DOCA_LINK_ARGS) 

static:
	$(CC) $(COMMON_CFLAGS) -c ./vbdev_passthru_rpc.c -o ./vbdev_passthru_rpc.o
	$(CC) $(COMMON_CFLAGS) -c ./vbdev_passthru.c -o ./vbdev_passthru.o
	$(AR) rcs ./libpassthru_external.a ./vbdev_passthru_rpc.o ./vbdev_passthru.o

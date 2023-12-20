#!/bin/bash
set -euxo pipefail

sudo systemctl stop mlnx_snap.service
sleep 5
sudo systemctl restart spdk_tgt.service
sleep 5

sudo spdk_rpc.py <<EOF
bdev_null_create Null0 131072 512
bdev_null_create Null1 131072 512
bdev_raid_create -n Raid -r 1 -b "Null0 Null1"
nvmf_create_transport -t RDMA -u 8192 -i 131072 -c 8192
nvmf_create_subsystem nqn.2016-06.io.spdk:cnode1 -a -s SPDK00000000000001 -d SPDK_Controller1
nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 Raid
nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t rdma -a 10.134.11.91 -s 4420
EOF

#!/bin/bash
set -euxo pipefail

sudo systemctl stop spdk_tgt.service
sleep 5
sudo systemctl restart mlnx_snap.service
sleep 5

sudo spdk_rpc.py <<EOF
bdev_null_create Null0 131072 512
bdev_null_create Null1 131072 512
bdev_raid_create -n Raid -r 1 -b "Null0 Null1"
EOF

sleep 5

sudo snap_rpc.py <<EOF
subsystem_nvme_create Mellanox_NVMe_SNAP "Mellanox NVMe SNAP Controller"
controller_nvme_create mlx5_0 --subsys_id 0 --pf_id 0
controller_nvme_namespace_attach -c NvmeEmu0pf0 spdk Raid 1
EOF

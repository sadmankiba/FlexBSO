#!/bin/bash
set -euxo pipefail

device="$1"
bs="$2"
rw="$3"

sudo fio \
	--name=f \
	--runtime=60 \
	--direct=1 \
	--iodepth=32 \
	--numjobs=4 \
	--end_fsync=1 \
	--filename="$device" \
	--rw="$rw" \
	--bs="$bs"

#!/bin/bash
set -euxo pipefail

device="$1"
prefix="$2"

for bs in 4k 16k 64k 1M; do
	for rw in read write; do
		./fio-multi.sh "$device" "$bs" "$rw" > "$prefix"-"$rw"-"$bs"-multi.txt
	done
done

for bs in 4k 16k 64k 1M; do
	for rw in read write; do
		./fio-single.sh "$device" "$bs" "$rw" > "$prefix"-"$rw"-"$bs"-single.txt
	done
done

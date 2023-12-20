# FlexBSO

FlexBSO (Flexible Block Storage Offload) is a solution to expose DPU-managed remote block storage to datacenter VMs. Traditionally, remote block storage is exported to VMs through hypervisor or host software. However, such solution consumes host CPU cycles, incurs latency overhead and is inflexible. FlexBSO uses SR-IOV to provide isolated storage to VMs,  NVIDIA SNAP library to provide storage emulation, and SPDK to design and connect with block storage. 

Project Contribution
* Designing FlexBSO that manages remote storage on the DPU and allows customizing the storage backend (e.g. encrypted storage, RAID etc.) keeping it transparent to VMs. 
* Demonstrating that our SNAP-based design achieves 3x throughput compared to NVMe-over-RDMA in multi-threaded scenario. 
* Designing two custom SPDK block devices- safe RAID5 and compression block device. 
    * Safe RAID5 block device performs parity check on read IO and notifies about parity mismatch. 
    * Compression block device uses hardware-accelearated compression with DOCA SDK to compress on write IO and decompress on read IO to save storage space.

## Directory Structure
```
/
|- snap-vs-rdma : Scripts and results for benchmarking SNAP and NVMe-over-RDMA with Fio
|- bdev-passthru-compress : A SPDK virtual block device that performs de-/compression on read/write
|- raid : A SPDK block device solution to check parity on full-stripe read in RAID5 
|- doca-compression :  A program to measure performance of hardware-accelerated compression in Bluefield-2 DPU
|- presentation.pdf : Project presentation
|- report.pdf : Project report
```

## Contributors

* [Vojtech Aschenbrenner](https://github.com/asch)
* [John Shawger](https://github.com/shawgerj)
* [Sadman Sakib](https://github.com/sadmankiba)
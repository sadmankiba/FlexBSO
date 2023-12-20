# DOCA Compression 

The app performs compression and decompression on a local file. Compression and decompression can be done in software with `zlib` library or with hardware accelerator using DOCA compress library. The app can be run on host or on Nvidia Bluefield-2 DPU. The `Makefile` was generated from `build.ninja` using `ninjatool.py` obtained from [`qemu` repository](https://github.com/bonzini/qemu/blob/meson-poc/scripts/ninjatool.py).

```sh
# Build
$ make
# Run
$ ./doca_compression_local
```
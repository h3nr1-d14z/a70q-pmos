# Kernel build env for a70q (4.14.190 lineage-22.2) — arm64 NATIVE (host M4 = target arm64)
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang-19 lld-19 llvm-19 \
      binutils binutils-arm-linux-gnueabi gcc-arm-linux-gnueabi \
      make bc bison flex libssl-dev libelf-dev cpio kmod python3 python-is-python3 \
      git ca-certificates curl lz4 xz-utils gzip file rsync \
    && rm -rf /var/lib/apt/lists/*
# unversioned names for kbuild
RUN for t in clang clang++ ld.lld llvm-ar llvm-nm llvm-objcopy llvm-objdump llvm-strip llvm-readelf; do \
      ln -sf /usr/bin/$t-19 /usr/local/bin/$t; done
WORKDIR /src

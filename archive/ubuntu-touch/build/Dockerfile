# Build env Halium 11 / a70q cho Mac Apple Silicon (Docker Desktop hoặc OrbStack).
# Host arm64 = target arm64 → kernel build NATIVE (không qemu, không cross toolchain khác).
# Base ubuntu:20.04 = đúng bộ deps mà docs.halium.org liệt kê cho halium-11, ít friction nhất.
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git gnupg \
    flex bison gperf build-essential zip unzip \
    zlib1g-dev gcc g++ libc6-dev \
    libncurses-dev libgl1-mesa-dev libxml2-utils \
    gcc-aarch64-linux-gnu \
    xsltproc fontconfig \
    libssl-dev libreadline-dev bc lzop lz4 cpio git-lfs \
    python3 python3-pip python-is-python3 \
    default-jdk-headless \
    ccache rsync sudo \
  && rm -rf /var/lib/apt/lists/*

# repo tool (Google)
RUN curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo \
      -o /usr/local/bin/repo && chmod a+x /usr/local/bin/repo

# mkbootimg + avbtool (không có trên PyPI — clone từ AOSP)
RUN git clone --depth=1 https://android.googlesource.com/platform/system/tools/mkbootimg /opt/mkbootimg \
 && ln -s /opt/mkbootimg/mkbootimg.py /usr/local/bin/mkbootimg \
 && git clone --depth=1 https://android.googlesource.com/platform/external/avb /opt/avb \
 && ln -s /opt/avb/avbtool.py /usr/local/bin/avbtool

# halium: wrapper gcc — path_interposer của A11 (LD_PRELOAD) chặn execvp("cc1") của gcc
# (cc1 không nằm trong whitelist PATH). Wrapper unset LD_PRELOAD rồi exec gcc thật;
# /usr/local/bin đứng trước /usr/bin trong PATH nên kbuild tìm thấy wrapper trước.
RUN printf '#!/bin/sh\nunset LD_PRELOAD\nexec /usr/bin/aarch64-linux-gnu-gcc "$@"\n' \
      > /usr/local/bin/aarch64-linux-gnu-gcc \
 && chmod +x /usr/local/bin/aarch64-linux-gnu-gcc

ENV USE_CCACHE=1 \
    CCACHE_DIR=/ccache \
    CCACHE_EXEC=/usr/bin/ccache \
    LANG=C.UTF-8

RUN git config --global user.name "builder" \
 && git config --global user.email "builder@localhost" \
 && git config --global color.ui false \
 && git lfs install

WORKDIR /work
CMD ["/bin/bash"]

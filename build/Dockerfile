# Build env Halium 11 / a70q cho Mac Apple Silicon (Docker Desktop hoặc OrbStack).
# Host arm64 = target arm64 → kernel build NATIVE (không qemu, không cross toolchain khác).
# Base ubuntu:20.04 = đúng bộ deps mà docs.halium.org liệt kê cho halium-11, ít friction nhất.
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git gnupg \
    flex bison gperf build-essential zip unzip \
    zlib1g-dev gcc-multilib g++-multilib libc6-dev \
    lib32ncurses-dev lib32z1-dev libgl1-mesa-dev libxml2-utils \
    xsltproc fontconfig \
    libssl-dev libreadline-dev bc lzop lz4 cpio \
    python3 python3-pip python-is-python3 \
    default-jdk-headless \
    ccache rsync sudo \
  && rm -rf /var/lib/apt/lists/*

# repo tool (Google)
RUN curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo \
      -o /usr/local/bin/repo && chmod a+x /usr/local/bin/repo

# mkbootimg + avbtool (python)
RUN pip3 install --no-cache-dir mkbootimg

ENV USE_CCACHE=1 \
    CCACHE_DIR=/ccache \
    CCACHE_EXEC=/usr/bin/ccache \
    LANG=C.UTF-8

RUN git config --global user.name "builder" \
 && git config --global user.email "builder@localhost" \
 && git config --global color.ui false

WORKDIR /work
CMD ["/bin/bash"]

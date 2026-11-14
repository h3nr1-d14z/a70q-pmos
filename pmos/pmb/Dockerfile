# pmbootstrap env (arm64 native). Run with --privileged (chroot mounts, loop devices).
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      git python3 sudo openssl ca-certificates curl kpartx cpio gzip xz-utils lz4 zstd file \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -m -s /bin/bash pmos && echo 'pmos ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/pmos
# pinned: the version every package/image in this repo was built with
RUN git clone --depth=1 --branch 3.12.1 https://gitlab.postmarketos.org/postmarketOS/pmbootstrap.git /opt/pmbootstrap \
    && ln -s /opt/pmbootstrap/pmbootstrap.py /usr/local/bin/pmbootstrap
USER pmos
WORKDIR /home/pmos

# SPDX-License-Identifier: GPL-2.0-only
FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential gcc-14 g++-14 clang flex bison gawk gettext git libncurses-dev \
    libssl-dev python3 python3-setuptools python3-dev python3-pyelftools rsync swig unzip zlib1g-dev \
    file wget curl ca-certificates bzip2 xz-utils patch perl time ccache autoconf \
    automake libtool texinfo pkg-config help2man libelf-dev device-tree-compiler \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -m -u 1001 builder

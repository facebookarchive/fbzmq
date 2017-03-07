#!/bin/bash

. "deps_common.sh"

sudo apt-get install libdouble-conversion-dev \
  libgtest-dev \
  libssl-dev \
  cmake \
  make \
  zip \
  git \
  autoconf \
  autoconf-archive \
  automake \
  libtool \
  g++ \
  libboost-all-dev \
  libevent-dev \
  flex \
  bison \
  libgoogle-glog-dev \
  libgflags-dev \
  liblz4-dev \
  liblzma-dev \
  scons \
  libkrb5-dev \
  libsnappy-dev \
  libsasl2-dev \
  libnuma-dev \
  pkg-config \
  zlib1g-dev \
  binutils-dev \
  libjemalloc-dev \
  libiberty-dev

# install gtest
pushd .
cd "/usr/src/gtest" || return
sudo cmake CMakeLists.txt
sudo make
for file in *.a; do
  sudo cp "$file" /usr/lib
done
popd

install_mstch ubuntu_16.04
install_zstd ubuntu_16.04
install_folly ubuntu_16.04
install_wangle ubuntu_16.04
install_libsodium ubuntu_16.04
install_libzmq ubuntu_16.04
install_fbthrift ubuntu_16.04

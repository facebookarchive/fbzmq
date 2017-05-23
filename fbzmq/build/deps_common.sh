#!/bin/bash
# Helpers used by deps_*.sh, meant to be sourced.

set -ex

BUILD_DIR="$(readlink -f "$(dirname "$0")")"
mkdir -p "$BUILD_DIR/deps"
cd "$BUILD_DIR/deps"

install_zstd() {
  pushd .
  if [[ ! -e "zstd" ]]; then
    git clone https://github.com/facebook/zstd
  fi
  cd zstd
  make
  make install
  popd
}

install_mstch() {
  pushd .
  if [[ ! -e "mstch" ]]; then
    git clone https://github.com/no1msd/mstch
  fi
  cd mstch
  cmake .
  make
  make install
  popd
}

install_wangle() {
  pushd .
  if [[ ! -e "wangle" ]]; then
    git clone https://github.com/facebook/wangle
  fi
  cd wangle/wangle
  git checkout v2017.03.20.00
  cmake .
  make
  make install
  popd
}

install_libzmq() {
  pushd .
  if [[ ! -e "libzmq" ]]; then
    git clone https://github.com/zeromq/libzmq
  fi
  cd libzmq
  ./autogen.sh
  ./configure
  make
  make check
  make install
  popd
}

install_libsodium() {
  pushd .
  if [[ ! -e "libsodium" ]]; then
    git clone https://github.com/jedisct1/libsodium --branch stable
  fi
  cd libsodium
  ./configure
  make
  make check
  make install
  popd
}

install_folly() {
  pushd .
  if [[ ! -e "folly" ]]; then
    git clone https://github.com/facebook/folly
  fi
  cd folly/folly
  git checkout v2017.03.20.00
  if [[ -x "./build/deps_$1.sh" ]] ; then
    "./build/deps_$1.sh"
  fi
  autoreconf -ivf
  ./configure
  make
  make install
  ldconfig
  popd
}

install_fbthrift() {
  pushd .
  if [[ ! -e "fbthrift" ]]; then
    git clone https://github.com/facebook/fbthrift
  fi
  cd fbthrift/thrift
  git checkout v2017.03.20.00
  if [[ -x "./build/deps_$1.sh" ]] ; then
    "./build/deps_$1.sh"
  fi
  autoreconf -if
  ./configure
  make
  make install
  ldconfig
  popd
}

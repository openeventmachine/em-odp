#!/bin/bash
set -e

#export ODP_VERSION=v1.23.0.0
export ODP_VERSION=master

export BUILD_DIR=$(readlink -e $(dirname $0))/build

mkdir ${BUILD_DIR} -p
pushd ${BUILD_DIR}
git clone --branch ${ODP_VERSION} --depth 1 https://github.com/OpenDataPlane/odp.git
pushd ./odp
./bootstrap
./configure \
	--prefix=${BUILD_DIR}/odp_install \
	--without-examples \
	--without-tests
make -j $(nproc)
make install
popd && popd

pushd ${BUILD_DIR}/../..
./bootstrap
./configure \
	--prefix=${BUILD_DIR}/em-odp_install \
	--with-odp-path=${BUILD_DIR}/odp_install
make -j $(nproc)
make install
popd

#!/usr/bin/bash

# exit when any command fails
set -e
# import the the script containing common functions
source ../../include/scripts.sh

# get the ibvBench source path via environment variable or default value
IBVB_SOURCE_PATH=$(realpath "${IBVB_SOURCE_PATH:-../../../}")

if [[ -f "${IBVB_SOURCE_PATH}/rdma-core/rc_pingpong.c" ]]; then
  echo "Found ibvBench at ${IBVB_SOURCE_PATH}"
else
  echo "Did not find ibvBench at ${IBVB_SOURCE_PATH}!"
  exit 1
fi

# create the ./init directory
mkdir_s ./init
# move to ./init directory
cd init

# setup module environment
#module purge # module purge is broken on campus cluster
module load gcc
module load openmpi
module load cmake
module load papi
export CC=gcc
export CXX=g++

# record build status
record_env

mkdir -p log
mv *.log log

# build FB
mkdir -p build
cd build
echo "Running cmake..."
IBVB_INSTALL_PATH=$(realpath "../install")
cmake -DCMAKE_INSTALL_PREFIX=${IBVB_INSTALL_PATH} \
      -DCMAKE_BUILD_TYPE=Release \
      -DUSE_PAPI=ON \
      -DLCM_PM_BACKEND=pmi2 \
      -L \
      ${IBVB_SOURCE_PATH} | tee init-cmake.log 2>&1 || { echo "cmake error!"; exit 1; }
cmake -LAH . >> init-cmake.log
echo "Running make..."
make VERBOSE=1 -j | tee init-make.log 2>&1 || { echo "make error!"; exit 1; }
#echo "Installing taskFlow to ${IBVB_INSTALL_PATH}"
#mkdir -p ${IBVB_INSTALL_PATH}
#make install > init-install.log 2>&1 || { echo "install error!"; exit 1; }
mv *.log ../log
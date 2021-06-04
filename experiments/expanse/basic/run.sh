#!/bin/bash

# exit when any command fails
set -e
# import the the script containing common functions
source ../../include/scripts.sh

task="basic.slurm"
sbatch_path=$(realpath "${sbatch_path:-.}")
exe_path=$(realpath "${exe_path:-init/build/benchmarks}")

if [[ -d "${exe_path}" ]]; then
  echo "Run ibv benchmarks at ${exe_path}"
else
  echo "Did not find benchmarks at ${exe_path}!"
  exit 1
fi

# create the ./run directory
mkdir_s ./run

module load python

for i in $(eval echo {1..${1:-1}}); do
  cd run
  sbatch ${sbatch_path}/${task} ${exe_path} || { echo "sbatch error!"; exit 1; }
  cd ../
done
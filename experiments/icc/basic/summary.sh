#!/bin/bash

# exit when any command fails
set -e
# import the the script containing common functions
source ../../include/scripts.sh

mkdir_s ./summary

if [[ -d "./init" ]]; then
  find ./init -name '*.log' -exec cp --parents \{\} ./summary \;
fi
if [[ -d "./run" ]]; then
  find ./run -name 'slurm_output.*' -exec cp --parents \{\} ./summary \;
fi
if [[ -d "./draw" ]]; then
  cp -r draw ./summary
fi
cp *.slurm ./summary
cp *.sh ./summary
cp *.py ./summary

tar -czf summary.tar.gz summary
rm -rf summary
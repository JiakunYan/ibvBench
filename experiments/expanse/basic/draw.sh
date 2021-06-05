#!/bin/bash

# exit when any command fails
set -e
# import the the script containing common functions
source ../../include/scripts.sh

task="basic"

mkdir_s draw
python3 parse_${task}.py
python3 draw_${task}.py
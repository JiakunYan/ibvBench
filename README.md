# ibvBench

This is a benchmark project for Infiniband.

Author: Jiakun Yan (jiakuny3@illinois.edu)

## Directory structure
- cmake_modules: contains some essential cmake scripts.
- modules: contains two essential submodule for this project:
    - log: the log system, borrowed from the `libfabric` project (https://ofiwg.github.io/libfabric/).
    - pmi: A pmi wrapper for PMI and PMI2. The user can choose which one to use by the
        cmake option `USE_PMI2`.
- benchmarks: The actual benchmarks. Currently, they are:
    - ibv_pingpong_sendrecv: pingpong benchmark for the IB channel semantic.
- rdma-core: benchmark examples, borrowed from the `rdma-core` project (https://github.com/linux-rdma/rdma-core).
- experiments: contains some useful scripts to run benchmarks on various platform.
    Currently, we have set up the scripts for
    - SDSC Expanse (https://www.sdsc.edu/support/user_guides/expanse.html)

## Building and running the benchmarks
This project uses the cmake build system. We have also set up the scripts to run benchmarks
on various platform. Take SDSC Expanse for example:
```
> cd /path/to/ibvBench/experiments/expanse
> ./init.sh # build this project in `init` directory
> ./run.sh  # submit some (by default, 7) sbatch scripts to slurm. Results will be in `run` directory.
> ./draw.sh # draw some summary figures in `draw` directory
```
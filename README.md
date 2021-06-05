# ibvBench

This is a benchmark suite for Infiniband.

Author: Jiakun Yan (jiakuny3@illinois.edu)

## Directory structure
- cmake_modules: contains some essential cmake scripts.
- modules: contains two essential submodule for this project:
    - log: the log system, borrowed from the `libfabric` project (https://ofiwg.github.io/libfabric/).
    - pmi: A pmi wrapper for PMI and PMI2. The user can choose which one to use by the
        cmake option `USE_PMI2`.
- benchmarks: The actual benchmarks. Currently, they are:
    - mpi_pingpong: pingpong benchmark for MPI Send/Recv
    - ibv_pingpong_sendrecv: pingpong benchmark for IB channel semantic.
    - ibv_pingpong_write: pingpong benchmark for RDMA Write (IBV_WR_RDMA_WRITE).
    - ibv_pingpong_write_imm: pingpong benchmark for signaled RDMA Write (IBV_WR_RDMA_WRITE_WITH_IMM).
    - ibv_pingpong_read: pingpong benchmark for RDMA Read (IBV_WR_RDMA_READ).
    - rendezvous: implementation of rendezvous protocols for sending long messages. It contains:
        - ibv_pingpong_rdv_write: four-step rendezvous protocol using RDMA Write (IBV_WR_RDMA_WRITE).
        - ibv_pingpong_rdv_write_imm: three-step rendezvous protocol using signaled RDMA Write (IBV_WR_RDMA_WRITE_WITH_IMM).
        - ibv_pingpong_rdv_read: three-step rendezvous protocol using RDMA Read (IBV_WR_RDMA_READ).
- rdma-core: benchmark examples, borrowed from the `rdma-core` project (https://github.com/linux-rdma/rdma-core).
- experiments: contains some useful scripts to run benchmarks on various platform.
    Currently, we have set up the scripts for
    - SDSC Expanse (https://www.sdsc.edu/support/user_guides/expanse.html)

## Building and running the benchmarks
This project uses the cmake build system. We have also set up the scripts to run benchmarks
on various platform. Take SDSC Expanse for example:
```
> cd /path/to/ibvBench/experiments/expanse/basic
> ./init.sh # build this project in `init` directory
> ./run.sh  # submit some (by default, 7) sbatch scripts to slurm. Results will be in `run` directory.
> ./draw.sh # draw some summary figures in `draw` directory
```
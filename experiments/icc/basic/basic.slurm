#!/bin/bash
#SBATCH --partition=cs
#SBATCH --time=5:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --job-name=basic
#SBATCH --output=slurm_output.%x-o%j
#SBATCH --error=slurm_error.%x-o%j

path_to_exe=${1:-./init/build/benchmarks}

echo "srun -n 2 mpi_pingpong -t 1"
srun -n 2 ${path_to_exe}/mpi_pingpong -t 1
echo "srun -n 2 ibv_pingpong_sendrecv -t 1"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv -t 1
echo "srun -n 2 ibv_pingpong_write -t 1"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_write -t 1
echo "srun -n 2 ibv_pingpong_write_imm -t 1"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_write_imm -t 1

echo "srun -n 2 mpi_pingpong -t 0"
srun -n 2 ${path_to_exe}/mpi_pingpong -t 0
echo "srun -n 2 ibv_pingpong_sendrecv -t 0"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv -t 0
echo "srun -n 2 ibv_pingpong_write -t 0"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_write -t 0
echo "srun -n 2 ibv_pingpong_write_imm -t 0"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_write_imm -t 0

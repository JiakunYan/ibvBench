#!/bin/bash
#SBATCH --partition=compute
#SBATCH --account=uic193
#SBATCH --time=5:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --job-name=basic
#SBATCH --output=slurm_output.%x-o%j
#SBATCH --error=slurm_error.%x-o%j

path_to_exe=${1:-./init/build/benchmarks}
echo "srun -n 2 ibv_pingpong_sendrecv"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv
echo "srun -n 2 ibv_pingpong_write"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_write

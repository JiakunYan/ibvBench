#!/bin/bash
#SBATCH --partition=compute
#SBATCH --account=uic193
#SBATCH --time=5:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --job-name=inline_size
#SBATCH --output=slurm_output.%x-o%j
#SBATCH --error=slurm_error.%x-o%j

path_to_exe=${1:-./init/build/benchmarks}

echo "srun -n 2 mpi_pingpong -t 1 UCX_TLS=rc_v"
UCX_TLS=rc_v srun -n 2 --mpi=pmi2 ${path_to_exe}/mpi_pingpong -t 1
echo "srun -n 2 ibv_pingpong_sendrecv -t 1 -i 0"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv -t 1 -i 0
echo "srun -n 2 ibv_pingpong_sendrecv -t 1 -i 60"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv -t 1 -i 60
echo "srun -n 2 ibv_pingpong_sendrecv -t 1 -i 236"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv -t 1 -i 236
echo "srun -n 2 ibv_pingpong_sendrecv -t 1 -i 512"
srun -n 2 --mpi=pmi2 ${path_to_exe}/ibv_pingpong_sendrecv -t 1 -i 512

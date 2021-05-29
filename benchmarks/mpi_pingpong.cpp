#include <mpi.h>
#include <unistd.h>
#include "bench_common.hpp"
#include "mlog.h"

#define MPI_CHECK(stmt)                                          \
do {                                                             \
   int mpi_errno = (stmt);                                       \
   if (MPI_SUCCESS != mpi_errno) {                               \
       fprintf(stderr, "[%s:%d] MPI call failed with %d \n",     \
        __FILE__, __LINE__,mpi_errno);                           \
       exit(EXIT_FAILURE);                                       \
   }                                                             \
} while (0)

using namespace bench;

struct Config {
    bool touch_data = true;
    int min_msg_size = 8;
    int max_msg_size = 64 * 1024;
};

Config parseArgs(int argc, char **argv) {
    Config config;
    int opt;
    opterr = 0;

    struct option long_options[] = {
            {"min-msg-size", required_argument, 0, 'a'},
            {"max-msg-size", required_argument, 0, 'b'},
            {"touch-data",   required_argument, 0, 't'},
    };
    while ((opt = getopt_long(argc, argv, "t:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                config.min_msg_size = atoi(optarg);
                break;
            case 'b':
                config.max_msg_size = atoi(optarg);
                break;
            case 't':
                config.touch_data = atoi(optarg);
                break;
            default:
                break;
        }
    }
    return config;
}

void run(const Config &config) {
    int rank, nranks;
    MPI_CHECK(MPI_Init(0, 0));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &nranks));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MLOG_Assert(nranks == 2, "This benchmark requires exactly two processes\n");
    char value = 'a' + rank;
    char peer_value = 'a' + 1 - rank;
    char *buf;
    const int PAGE_SIZE = sysconf(_SC_PAGESIZE);
    posix_memalign((void **)&buf, PAGE_SIZE, config.max_msg_size);
    if (!buf) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(EXIT_FAILURE);
    }

    if (rank == 0) {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, true, [&](int msg_size, int iter) {
          if (config.touch_data) write_buffer((char*) buf, msg_size, value);
          MPI_CHECK(MPI_Send(buf, msg_size, MPI_CHAR, 1 - rank, 1, MPI_COMM_WORLD));
          MPI_CHECK(MPI_Recv(buf, msg_size, MPI_CHAR, 1 - rank, 1, MPI_COMM_WORLD, NULL));
          if (config.touch_data) check_buffer((char*) buf, msg_size, peer_value);
        });
    } else {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, false, [&](int msg_size, int iter) {
          MPI_CHECK(MPI_Recv(buf, msg_size, MPI_CHAR, 1 - rank, 1, MPI_COMM_WORLD, NULL));
          if (config.touch_data) check_buffer((char*) buf, msg_size, peer_value);
          if (config.touch_data) write_buffer((char*) buf, msg_size, value);
          MPI_CHECK(MPI_Send(buf, msg_size, MPI_CHAR, 1 - rank, 1, MPI_COMM_WORLD));
        });
    }
    free(buf);
    MPI_CHECK(MPI_Finalize());
}

int main(int argc, char **argv) {
    Config config = parseArgs(argc, argv);
    run(config);
    return 0;
}
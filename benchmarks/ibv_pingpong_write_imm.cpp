#include "ibv_common.hpp"
#include "bench_common.hpp"

using namespace std;
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
            case 'b':
                config.max_msg_size = atoi(optarg);
            case 't':
                config.touch_data = atoi(optarg);
                break;
            default:
                break;
        }
    }
    return config;
}

int run(Config config) {
    ibv::Device device;
    ibv::init(NULL, &device);
    int rank = pmi_get_rank();
    int nranks = pmi_get_size();
    MLOG_Assert(nranks == 2, "This benchmark requires exactly two processes\n");
    char value = 'a' + rank;
    char peer_value = 'a' + 1 - rank;
    memset(device.mr_addr, 0, ibv::MR_SIZE);
    pmi_barrier();

    if (rank == 0) {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, true, [&](int msg_size, int iter) {
            struct ibv_wc wc;
            // post one write
            if (config.touch_data) write_buffer((char*) device.mr_addr, msg_size, value);
            int ret = ibv::postWriteImm(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->rkey,
                                     device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, 77 + rank, NULL);
            MLOG_Assert(ret == 0, "Post Write failed!");

            // wait for write to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Send completion failed!");

            // wait for remote write to complete
            wc = ibv::pollCQ(device.recv_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV_RDMA_WITH_IMM, "Recv completion failed!");
            // optionally post recv buffers
            --device.posted_recv_num;
            ibv::checkAndPostRecvs(&device);
            if (config.touch_data) check_buffer((char*) device.mr_addr, msg_size, peer_value);
        });
    } else {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, false, [&](int msg_size, int iter) {
            int ne;
            struct ibv_wc wc;
            // wait for one recv to complete
            wc = ibv::pollCQ(device.recv_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV_RDMA_WITH_IMM,
                      "Recv completion failed!");
            // optionally post recv buffers
            --device.posted_recv_num;
            ibv::checkAndPostRecvs(&device);
            if (config.touch_data) check_buffer((char*) device.mr_addr, msg_size, peer_value);

            // post one write
            if (config.touch_data) write_buffer((char*) device.mr_addr, msg_size, value);
            int ret = ibv::postWriteImm(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->rkey,
                                   device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, 77 + rank, NULL);
            MLOG_Assert(ret == 0, "Post Write failed!");

            // wait for write to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Send completion failed!");
        });
    }

    pmi_barrier();
    ibv::finalize(&device);
    return 0;
}

int main(int argc, char **argv) {
    Config config = parseArgs(argc, argv);
    run(config);
    return 0;
}
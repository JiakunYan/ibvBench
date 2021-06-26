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

int run(Config config) {
    ibv::Device device;
    ibv::DeviceConfig deviceConfig;
    deviceConfig.mr_size = config.max_msg_size;
    ibv::init(NULL, &device, deviceConfig);
    int rank = lcm_pm_get_rank();
    int nranks = lcm_pm_get_size();
    MLOG_Assert(nranks == 2, "This benchmark requires exactly two processes\n");
    char value = 'a' + rank;
    char peer_value = 'a' + 1 - rank;
    volatile char *buf = (char*) device.mr_addr;
    memset(device.mr_addr, 0, config.max_msg_size);
    lcm_pm_barrier();
    ibv::checkAndPostRecvs(&device, device.mr_addr, device.mr_size, device.dev_mr->lkey, device.mr_addr);

    if (rank == 0) {
        // wait for the start signal
        struct ibv_wc wc = ibv::pollCQ(device.recv_cq);
        MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv completion failed!\b");
        --device.posted_recv_num;
        ibv::checkAndPostRecvs(&device, device.mr_addr, device.mr_size, device.dev_mr->lkey, device.mr_addr);

        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, true, [&](int msg_size, int iter) {
            struct ibv_wc wc;
            // post one read
            if (config.touch_data) write_buffer((char*) device.mr_addr, msg_size, value);
            int ret = ibv::postRead(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->lkey,
                                     device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, NULL);
            MLOG_Assert(ret == 0, "Post Read failed!");

            // wait for read to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_READ, "Read completion failed! %d %d\n", wc.status, wc.opcode);
            if (config.touch_data) check_buffer((char*) device.mr_addr, msg_size, peer_value);
        });

        // tell the other to finish
        ibv::postSend(&device, 1-rank, device.mr_addr, ibv::CACHE_LINE_SIZE, device.dev_mr->lkey, NULL);
        wc = ibv::pollCQ(device.send_cq);
        MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send completion failed!\n");
    } else {
        if (config.touch_data) write_buffer((char*) device.mr_addr, config.max_msg_size, value);
        // tell the other to start
        ibv::postSend(&device, 1-rank, device.mr_addr, ibv::CACHE_LINE_SIZE, device.dev_mr->lkey, NULL);
        struct ibv_wc wc = ibv::pollCQ(device.send_cq);
        MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send completion failed!\n");
        // wait for the finish signal
        wc = ibv::pollCQ(device.recv_cq);
        MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv completion failed!\b");
        --device.posted_recv_num;
        ibv::checkAndPostRecvs(&device, device.mr_addr, device.mr_size, device.dev_mr->lkey, device.mr_addr);
    }

    lcm_pm_barrier();
    ibv::finalize(&device);
    return 0;
}

int main(int argc, char **argv) {
    init(false);
    Config config = parseArgs(argc, argv);
    run(config);
    finalize();
    return 0;
}
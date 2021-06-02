#include "ibv_common.hpp"
#include "bench_common.hpp"

using namespace std;
using namespace bench;

struct Config {
    bool touch_data = true;
    int min_msg_size = 8;
    int max_msg_size = 64 * 1024;
    int inline_size = 220;
};

Config parseArgs(int argc, char **argv) {
    Config config;
    int opt;
    opterr = 0;

    struct option long_options[] = {
            {"min-msg-size", required_argument, 0, 'a'},
            {"max-msg-size", required_argument, 0, 'b'},
            {"touch-data",   required_argument, 0, 't'},
            {"inline-size",  required_argument, 0, 'i'},
    };
    while ((opt = getopt_long(argc, argv, "t:i:", long_options, NULL)) != -1) {
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
            case 'i':
                config.inline_size = atoi(optarg);
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
    deviceConfig.inline_size = config.inline_size;
    deviceConfig.mr_size = config.max_msg_size;
    ibv::init(NULL, &device, deviceConfig);
    int rank = pmi_get_rank();
    int nranks = pmi_get_size();
    MLOG_Assert(nranks == 2, "This benchmark requires exactly two processes\n");
    char value = 'a' + rank;
    char peer_value = 'a' + 1 - rank;
    volatile char *buf = (char*) device.mr_addr;
    memset(device.mr_addr, 0, config.max_msg_size);
    pmi_barrier();
    ibv::checkAndPostRecvs(&device, device.mr_addr, device.mr_size, device.dev_mr->lkey, device.mr_addr);

    if (rank == 0) {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, true, [&](int msg_size, int iter) {
            struct ibv_wc wc;
            // post one write
            if (config.touch_data) {
                write_buffer((char*) device.mr_addr, msg_size, value);
            } else {
                buf[0] = value;
                buf[msg_size - 1] = value;
            }
            int ret = ibv::postWrite(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->lkey,
                                     device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, NULL);
            MLOG_Assert(ret == 0, "Post Write failed!");

            // wait for write to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Send completion failed!");

            // wait for remote write to complete
            while (!(buf[msg_size-1] == 'b' && buf[0] == 'b')) continue;
            if (config.touch_data) check_buffer((char*) device.mr_addr, msg_size, peer_value);
        });
    } else {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, false, [&](int msg_size, int iter) {
            int ne;
            struct ibv_wc wc;
            // wait for remote write to complete
            volatile char *buf = (char*) device.mr_addr;
            while (!(buf[msg_size-1] == 'a' && buf[0] == 'a')) continue;
            if (config.touch_data) check_buffer((char*) device.mr_addr, msg_size, peer_value);

            // post one write
            if (config.touch_data) {
                write_buffer((char*) device.mr_addr, msg_size, value);
            } else {
                buf[0] = value;
                buf[msg_size - 1] = value;
            }
            int ret = ibv::postWrite(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->lkey,
                                   device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, NULL);
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
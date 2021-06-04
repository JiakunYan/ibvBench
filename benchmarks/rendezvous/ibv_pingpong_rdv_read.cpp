#include "bench_common.hpp"
#include "ibv_common.hpp"
using namespace std;
using namespace bench;
using namespace ibv;

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

struct SendCtx {
    void *buf;
    uint32_t size;
    ibv_mr *mr;
    void *user_context;
};

struct RecvCtx {
    void *buf;
    uint32_t size;
    ibv_mr *mr;
    void *user_context;
    uintptr_t send_ctx;
};

struct RTSMsg {
    uintptr_t send_ctx; // 8 bytes
    uintptr_t send_buf; // 8 bytes
    uint32_t size; // 4 bytes
    uint32_t rkey; // 4 bytes
};

struct FINMsg {
    uintptr_t send_ctx;
};

enum MsgType {
    MSG_RTS,
    MSG_FIN
};

RTSMsg *rtsMsg;
FINMsg *finMsg;
void *control_recv_buf;

struct ibv_wc pollRecvCQ(Device *device) {
    struct ibv_wc wc = pollCQ(device->recv_cq);
    MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv RTS completion failed!\n");
    --device->posted_recv_num;
    checkAndPostRecvs(device, control_recv_buf, CACHE_LINE_SIZE, device->dev_mr->lkey, control_recv_buf);
    return wc;
}

void postRTS(Device *device, int rank, void *buf, uint32_t size, struct ibv_mr *mr, void *user_context) {
    SendCtx *ctx = (SendCtx*) malloc(sizeof(SendCtx));
    ctx->buf = buf;
    ctx->size = size;
    ctx->mr = mr;
    ctx->user_context = user_context;
    rtsMsg->send_ctx = (uintptr_t) ctx;
    rtsMsg->send_buf = (uintptr_t) buf;
    rtsMsg->size = size;
    rtsMsg->rkey = mr->rkey;
    int ret = postSendImm(device, rank, rtsMsg, sizeof(RTSMsg), device->dev_mr->lkey, MSG_RTS, ctx);
    MLOG_Assert(ret == 0, "");
}

void handleRTS(Device *device, struct ibv_wc wc, void *buf, uint32_t size, struct ibv_mr *mr, void *user_context) {
    MLOG_Assert(wc.opcode == IBV_WC_RECV && wc.imm_data == MSG_RTS, "");
    RTSMsg *recvRTSMsg = (RTSMsg*) wc.wr_id;
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    MLOG_Assert(recvRTSMsg->size <= size, "");
    RecvCtx *ctx = (RecvCtx*) malloc(sizeof(RecvCtx));
    ctx->buf = buf;
    ctx->size = size;
    ctx->mr = mr;
    ctx->user_context = user_context;
    ctx->send_ctx = recvRTSMsg->send_ctx;
    int ret = postRead(device, src_rank, buf, size, mr->lkey,
                       recvRTSMsg->send_buf, recvRTSMsg->rkey, ctx);
    MLOG_Assert(ret == 0, "");
}

int handleReadCompletion(Device *device, struct ibv_wc wc) {
    MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_READ, "");
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    RecvCtx *ctx = (RecvCtx*) wc.wr_id;
    finMsg->send_ctx = ctx->send_ctx;
    delete ctx;
    return postSendImm(device, src_rank, finMsg, sizeof(FINMsg),
                       device->dev_mr->lkey, MSG_FIN, NULL);
}

void handleFIN(struct ibv_wc wc) {
    MLOG_Assert(wc.imm_data == MSG_FIN, "Recv FIN failed");
    FINMsg *recvFINMsg = (FINMsg*) wc.wr_id;
    SendCtx *ctx = (SendCtx*) recvFINMsg->send_ctx;
    delete ctx;
}

int run(Config config) {
    ibv::Device device;
    ibv::DeviceConfig deviceConfig;
    deviceConfig.mr_size = CACHE_LINE_SIZE * 4 + config.max_msg_size * 2;
    ibv::init(NULL, &device, deviceConfig);
    int rank = pmi_get_rank();
    int nranks = pmi_get_size();
    MLOG_Assert(nranks == 2, "This benchmark requires exactly two processes\n");
    char value = 'a' + rank;
    char peer_value = 'a' + 1 - rank;

    MLOG_Assert(sizeof(RTSMsg) <= CACHE_LINE_SIZE, "");
    MLOG_Assert(sizeof(FINMsg) <= CACHE_LINE_SIZE, "");
    MLOG_Assert(device.mr_size >= CACHE_LINE_SIZE * 3 + config.max_msg_size * 2, "");
    char *ptr = (char*) device.mr_addr;
    rtsMsg = (RTSMsg*) ptr;
    ptr += CACHE_LINE_SIZE;
    finMsg = (FINMsg*) ptr;
    ptr += CACHE_LINE_SIZE;
    control_recv_buf = (void*) ptr;
    ptr += CACHE_LINE_SIZE;
    void *send_buf = ptr;
    ptr += config.max_msg_size;
    void *recv_buf = ptr;
    checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);

    if (rank == 0) {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, true, [&](int msg_size, int iter) {
          struct ibv_wc wc;
          // post one rendezvous send
          if (config.touch_data) write_buffer((char*) send_buf, msg_size, value);
          postRTS(&device, 1-rank, send_buf, msg_size, device.dev_mr, NULL);
          // wait for send RTS to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTS completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for recv FIN to complete
          wc = pollRecvCQ(&device);
          handleFIN(wc);

          // receive a rendezvous recv
          // wait for RTS, post RDMA_Read
          wc = pollRecvCQ(&device);
          handleRTS(&device, wc, recv_buf, msg_size, device.dev_mr, NULL);
          // wait for send RDMA_Read to complete, post FIN
          wc = pollCQ(device.send_cq);
          handleReadCompletion(&device, wc);
          // wait for send FIN to complete, check buffer
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send FIN completion failed!\n");
          if (config.touch_data) check_buffer((char*) recv_buf, msg_size, peer_value);
        });
    } else {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, false, [&](int msg_size, int iter) {
          struct ibv_wc wc;
          // receive a rendezvous recv
          // wait for RTS, post RDMA_Read
          wc = pollRecvCQ(&device);
          handleRTS(&device, wc, recv_buf, msg_size, device.dev_mr, NULL);
          // wait for send RDMA_Read to complete, post FIN
          wc = pollCQ(device.send_cq);
          handleReadCompletion(&device, wc);
          // wait for send FIN to complete, check buffer
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send FIN completion failed!\n");
          if (config.touch_data) check_buffer((char*) recv_buf, msg_size, peer_value);

          // post one rendezvous send
          if (config.touch_data) write_buffer((char*) send_buf, msg_size, value);
          postRTS(&device, 1-rank, send_buf, msg_size, device.dev_mr, NULL);
          // wait for send RTS to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTS completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for recv FIN to complete
          wc = pollRecvCQ(&device);
          handleFIN(wc);
        });
    }

    ibv::finalize(&device);
    return 0;
}

int main(int argc, char **argv) {
    Config config = parseArgs(argc, argv);
    run(config);
    return 0;
}
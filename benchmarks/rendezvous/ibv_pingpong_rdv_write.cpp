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
    uint32_t lkey;
    void *user_context;
    uintptr_t recv_ctx; // 8 bytes
};

struct RecvCtx {
    void *buf;
    uint32_t size;
    void *user_context;
};

struct RTSMsg {
    uintptr_t send_ctx; // 8 bytes
    uint32_t size; // 8 bytes
};

struct RTRMsg {
    uintptr_t send_ctx; // 8 bytes
    uintptr_t recv_ctx; // 8 bytes
    uintptr_t remote_addr; // 8 bytes
    uint32_t rkey; // 4 bytes
};

struct FINMsg {
    uintptr_t recv_ctx;
};

enum MsgType {
    MSG_RTS,
    MSG_RTR,
    MSG_FIN
};

RTSMsg *rtsMsg;
RTRMsg *rtrMsg;
FINMsg *finMsg;
void *control_recv_buf;

int postRTS(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey, void *user_context) {
    SendCtx *ctx = (SendCtx*) malloc(sizeof(SendCtx));
    ctx->buf = buf;
    ctx->size = size;
    ctx->lkey = lkey;
    ctx->user_context = user_context;
    rtsMsg->send_ctx = (uintptr_t) ctx;
    rtsMsg->size = size;
    return postSendImm(device, rank, rtsMsg, sizeof(RTSMsg), device->dev_mr->lkey, MSG_RTS, NULL);
}

int handleRTS(Device *device, struct ibv_wc wc, void *buf, uint32_t size, uint32_t lkey, void *user_context) {
    MLOG_Assert(wc.opcode == IBV_WC_RECV, "");
    MLOG_Assert(wc.imm_data == MSG_RTS, "");
    RTSMsg *recvRTSMsg = (RTSMsg*) wc.wr_id;
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    RecvCtx *ctx = (RecvCtx*) malloc(sizeof(RecvCtx));
    MLOG_Assert(recvRTSMsg->size <= size, "");
    ctx->size = size;
    ctx->buf = buf;
    ctx->user_context = user_context;
    rtrMsg->send_ctx = recvRTSMsg->send_ctx;
    rtrMsg->recv_ctx = (uintptr_t) ctx;
    rtrMsg->remote_addr = (uintptr_t) buf;
    rtrMsg->rkey = device->dev_mr->rkey;
    return postSendImm(device, src_rank, rtrMsg, sizeof(RTRMsg),
                       device->dev_mr->lkey, MSG_RTR, NULL);
}

int handleRTR(Device *device, struct ibv_wc wc) {
    MLOG_Assert(wc.opcode == IBV_WC_RECV, "");
    MLOG_Assert(wc.imm_data == MSG_RTR, "");
    RTRMsg *recvRTRMsg = (RTRMsg*) wc.wr_id;
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    SendCtx *ctx = (SendCtx*)recvRTRMsg->send_ctx;
    ctx->recv_ctx = recvRTRMsg->recv_ctx;
    return postWrite(device, src_rank, ctx->buf, ctx->size, ctx->lkey,
                     recvRTRMsg->remote_addr, recvRTRMsg->rkey, ctx);
}

int handleWriteCompletion(Device *device, struct ibv_wc wc) {
    MLOG_Assert(wc.opcode == IBV_WC_RDMA_WRITE, "");
    SendCtx *ctx = (SendCtx*) wc.wr_id;
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    finMsg->recv_ctx = ctx->recv_ctx;
    return postSendImm(device, src_rank, finMsg, sizeof(FINMsg),
                       device->dev_mr->lkey, MSG_FIN, ctx);
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
    MLOG_Assert(sizeof(RTRMsg) <= CACHE_LINE_SIZE, "");
    MLOG_Assert(sizeof(FINMsg) <= CACHE_LINE_SIZE, "");
    MLOG_Assert(device.mr_size >= CACHE_LINE_SIZE * 4 + config.max_msg_size * 2, "");
    char *ptr = (char*) device.mr_addr;
    rtsMsg = (RTSMsg*) ptr;
    ptr += CACHE_LINE_SIZE;
    rtrMsg = (RTRMsg*) ptr;
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
            int ret;
            // post one rendezvous send
            if (config.touch_data) write_buffer((char*) send_buf, msg_size, value);
            ret = postRTS(&device, 1-rank, send_buf, msg_size, device.dev_mr->lkey, NULL);
            MLOG_Assert(ret == 0, "Post RTS failed!\n");
            // wait for send to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTS completion failed! %d %d\n", wc.status, wc.opcode);
            // wait for one recv to complete
            wc = ibv::pollCQ(device.recv_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv RTR completion failed!\n");
            --device.posted_recv_num;
            checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);;
            // handle RTR and post write
            ret = handleRTR(&device, wc);
            MLOG_Assert(ret == 0, "Handle RTR and post Write failed!\n");
            // wait for write to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Write completion failed! %d %d\n", wc.status, wc.opcode);
            ret = handleWriteCompletion(&device, wc);
            MLOG_Assert(ret == 0, "Handle Write Completion and post Fin failed!\n");
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send FIN completion failed!\n");

            // receive a rendezvous recv
            // wait for RTS, post RTR
            wc = ibv::pollCQ(device.recv_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv RTS completion failed!\n");
            --device.posted_recv_num;
            checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);;
            ret = handleRTS(&device, wc, recv_buf, msg_size, device.dev_mr->lkey, NULL);
            MLOG_Assert(ret == 0, "Handle RTS and Send RTR failed!\n");
            // wait for send to complete
            wc = ibv::pollCQ(device.send_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTR completion failed! %d %d\n", wc.status, wc.opcode);
            // wait for FIN, check buffer
            wc = ibv::pollCQ(device.recv_cq);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv FIN completion failed!\n");
            --device.posted_recv_num;
            checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);;
            MLOG_Assert(wc.imm_data == MSG_FIN, "Recv FIN failed");
            if (config.touch_data) check_buffer((char*) recv_buf, msg_size, peer_value);
        });
    } else {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, false, [&](int msg_size, int iter) {
          struct ibv_wc wc;
          int ret;
          // receive a rendezvous recv
          // wait for RTS, post RTR
          wc = ibv::pollCQ(device.recv_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv RTS completion failed!\n");
          --device.posted_recv_num;
          ibv::checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);;
          ret = handleRTS(&device, wc, recv_buf, msg_size, device.dev_mr->lkey, NULL);
          MLOG_Assert(ret == 0, "Handle RTS and Send RTR failed!\n");
          // wait for send to complete
          wc = ibv::pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTR completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for FIN, check buffer
          wc = ibv::pollCQ(device.recv_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv FIN completion failed!\n");
          --device.posted_recv_num;
          checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);;
          MLOG_Assert(wc.imm_data == MSG_FIN, "Recv FIN failed");
          if (config.touch_data) check_buffer((char*) recv_buf, msg_size, peer_value);

          // post one rendezvous send
          if (config.touch_data) write_buffer((char*) send_buf, msg_size, value);
          ret = postRTS(&device, 1-rank, send_buf, msg_size, device.dev_mr->lkey, NULL);
          MLOG_Assert(ret == 0, "Post RTS failed!\n");
          // wait for send to complete
          wc = ibv::pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTS completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for one recv to complete
          wc = ibv::pollCQ(device.recv_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv RTR completion failed!\n");
          --device.posted_recv_num;
          checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);;
          // handle RTR and post write
          ret = handleRTR(&device, wc);
          MLOG_Assert(ret == 0, "Handle RTR and post Write failed!\n");
          // wait for write to complete
          wc = ibv::pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Write completion failed! %d %d\n", wc.status, wc.opcode);
          ret = handleWriteCompletion(&device, wc);
          MLOG_Assert(ret == 0, "Handle Write Completion and post Fin failed!\n");
          wc = ibv::pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send FIN completion failed!\n");
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
#include "bench_common.hpp"
#include "ibv_common.hpp"
#include "lcm_archive.h"
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
};

struct RTSMsg {
    uintptr_t send_ctx; // 8 bytes
    uint32_t size; // 8 bytes
};

struct RTRMsg {
    uintptr_t send_ctx; // 8 bytes
    uintptr_t remote_addr; // 8 bytes
    uint32_t rkey; // 4 bytes
    uint16_t recv_ctx_key; // 2 bytes
};

enum MsgType {
    MSG_RTS,
    MSG_RTR,
};

LCM_archive_t recv_ctx_archive;
RTSMsg *rtsMsg;
RTRMsg *rtrMsg;
void *control_recv_buf;

struct ibv_wc pollRecvCQ(Device *device) {
    struct ibv_wc wc = pollCQ(device->recv_cq);
    MLOG_Assert(wc.status == IBV_WC_SUCCESS, "Recv RTS completion failed!\n");
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
    rtsMsg->size = size;
    int ret = postSendImm(device, rank, rtsMsg, sizeof(RTSMsg), device->dev_mr->lkey, MSG_RTS, NULL);
    MLOG_Assert(ret == 0, "\n");
}

void handleRTS(Device *device, struct ibv_wc wc, void *buf, uint32_t size, struct ibv_mr *mr, void *user_context) {
    MLOG_Assert(wc.opcode == IBV_WC_RECV && wc.imm_data == MSG_RTS, "");
    RTSMsg *recvRTSMsg = (RTSMsg*) wc.wr_id;
    MLOG_Assert(recvRTSMsg->size <= size, "");
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    RecvCtx *ctx = (RecvCtx*) malloc(sizeof(RecvCtx));
    uint64_t ctx_key;
    int ret = LCM_archive_put(recv_ctx_archive, (uintptr_t)ctx, &ctx_key);
    MLOG_Assert(ret == LCM_SUCCESS, "Archive is full!\n");
    ctx->size = size;
    ctx->buf = buf;
    ctx->mr = mr;
    ctx->user_context = user_context;
    rtrMsg->send_ctx = recvRTSMsg->send_ctx;
    rtrMsg->remote_addr = (uintptr_t) buf;
    rtrMsg->rkey = mr->rkey;
    rtrMsg->recv_ctx_key = (uint16_t) ctx_key;
    ret= postSendImm(device, src_rank, rtrMsg, sizeof(RTRMsg),
                         device->dev_mr->lkey, MSG_RTR, NULL);
    MLOG_Assert(ret == 0, "\n");
}

void handleRTR(Device *device, struct ibv_wc wc) {
    MLOG_Assert(wc.opcode == IBV_WC_RECV && wc.imm_data == MSG_RTR, "");
    RTRMsg *recvRTRMsg = (RTRMsg*) wc.wr_id;
    int src_rank = device->qp2rank[wc.qp_num % device->qp2rank_mod];
    SendCtx *ctx = (SendCtx*)recvRTRMsg->send_ctx;
    int ret = postWriteImm(device, src_rank, ctx->buf, ctx->size, ctx->mr->lkey,
                           recvRTRMsg->remote_addr, recvRTRMsg->rkey, recvRTRMsg->recv_ctx_key, NULL);
    MLOG_Assert(ret == 0, "\n");
    delete ctx;
}

void handleWriteImm(struct ibv_wc wc) {
    MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV_RDMA_WITH_IMM, "Recv WriteImm failed");
    uint64_t ctx_key = wc.imm_data;
    RecvCtx *ctx = (RecvCtx*) LCM_archive_remove(recv_ctx_archive, ctx_key);
    delete ctx;
}

int run(Config config) {
    Device device;
    DeviceConfig deviceConfig;
    deviceConfig.mr_size = CACHE_LINE_SIZE * 3 + config.max_msg_size * 2;
    init(NULL, &device, deviceConfig);
    int rank = pmi_get_rank();
    int nranks = pmi_get_size();
    MLOG_Assert(nranks == 2, "This benchmark requires exactly two processes\n");
    char value = 'a' + rank;
    char peer_value = 'a' + 1 - rank;

    MLOG_Assert(sizeof(RTSMsg) <= CACHE_LINE_SIZE, "");
    MLOG_Assert(sizeof(RTRMsg) <= CACHE_LINE_SIZE, "");
    MLOG_Assert(device.mr_size >= CACHE_LINE_SIZE * 3 + config.max_msg_size * 2, "");
    char *ptr = (char*) device.mr_addr;
    rtsMsg = (RTSMsg*) ptr;
    ptr += CACHE_LINE_SIZE;
    rtrMsg = (RTRMsg*) ptr;
    ptr += CACHE_LINE_SIZE;
    control_recv_buf = (void*) ptr;
    ptr += CACHE_LINE_SIZE;
    void *send_buf = ptr;
    ptr += config.max_msg_size;
    void *recv_buf = ptr;
    checkAndPostRecvs(&device, control_recv_buf, CACHE_LINE_SIZE, device.dev_mr->lkey, control_recv_buf);

    int ret = LCM_archive_init(&recv_ctx_archive, 10); // 1024 entry
    MLOG_Assert(ret == LCM_SUCCESS, "Initialize archive failed!\n");

    if (rank == 0) {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, true, [&](int msg_size, int iter) {
          struct ibv_wc wc;
          // post one rendezvous send
          if (config.touch_data) write_buffer((char*) send_buf, msg_size, value);
          postRTS(&device, 1-rank, send_buf, msg_size, device.dev_mr, NULL);
          // wait for send to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTS completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for one recv to complete
          wc = pollRecvCQ(&device);
          handleRTR(&device, wc);
          // wait for write to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "WriteImm completion failed!\n");

          // receive a rendezvous recv
          // wait for RTS, post RTR
          wc = pollRecvCQ(&device);
          handleRTS(&device, wc, recv_buf, msg_size, device.dev_mr, NULL);
          // wait for send to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTR completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for FIN, check buffer
          wc = pollRecvCQ(&device);
          handleWriteImm(wc);
          if (config.touch_data) check_buffer((char*) recv_buf, msg_size, peer_value);
        });
    } else {
        RUN_VARY_MSG({config.min_msg_size, config.max_msg_size}, false, [&](int msg_size, int iter) {
          struct ibv_wc wc;
          // receive a rendezvous recv
          // wait for RTS, post RTR
          wc = pollRecvCQ(&device);
          handleRTS(&device, wc, recv_buf, msg_size, device.dev_mr, NULL);
          // wait for send to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTR completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for FIN, check buffer
          wc = pollRecvCQ(&device);
          handleWriteImm(wc);
          if (config.touch_data) check_buffer((char*) recv_buf, msg_size, peer_value);

          // post one rendezvous send
          if (config.touch_data) write_buffer((char*) send_buf, msg_size, value);
          postRTS(&device, 1-rank, send_buf, msg_size, device.dev_mr, NULL);
          // wait for send to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send RTS completion failed! %d %d\n", wc.status, wc.opcode);
          // wait for one recv to complete
          wc = pollRecvCQ(&device);
          handleRTR(&device, wc);
          // wait for write to complete
          wc = pollCQ(device.send_cq);
          MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "WriteImm completion failed!\n");
        });
    }

    ret = LCM_archive_fini(&recv_ctx_archive); // 1024 entry
    MLOG_Assert(ret == LCM_SUCCESS, "Finalize archive failed!\n");
    finalize(&device);
    return 0;
}

int main(int argc, char **argv) {
    init(false);
    Config config = parseArgs(argc, argv);
    run(config);
    finalize();
    return 0;
}
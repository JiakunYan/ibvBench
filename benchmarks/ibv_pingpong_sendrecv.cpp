#include "ibv_common.hpp"
#include "bench_common.hpp"

using namespace std;
using namespace bench;

int min_msg_size = 8;
int max_msg_size = 64 * 1024;

int main() {
    ibv::Device device;
    ibv::init(NULL, &device);
    int rank = pmi_get_rank();
    int nranks = pmi_get_size();
    assert(nranks == 2);

    if (rank == 0) {
        RUN_VARY_MSG({min_msg_size, max_msg_size}, true, [&](int msg_size, int iter) {
            // post one send
            ibv::postSend(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->rkey, NULL);

            // wait for send to complete
            int ne;
            struct ibv_wc wc;
            do {
                ne = ibv_poll_cq(device.send_cq, 1, &wc);
                MLOG_Assert(ne >= 0, "Poll Send CQ failed %d\n", ne);
            } while (ne == 0);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND, "Send completion failed!");

            // wait for one recv to complete
            do {
                ne = ibv_poll_cq(device.recv_cq, 1, &wc);
                MLOG_Assert(ne >= 0, "Poll Recv CQ failed %d\n", ne);
            } while (ne == 0);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV, "Recv completion failed!");
            // optionally post recv buffers
            if (--device.posted_recv_num < ibv::MIN_RECV_NUM) {
                for (int j = device.posted_recv_num; j < ibv::MAX_RECV_NUM; ++j) {
                    int ret = ibv::postRecv(&device, device.mr_addr, device.mr_size, device.dev_mr->rkey, NULL);
                    MLOG_Assert(ret == 0, "Post Recv failed!");
                }
            }
        });
    } else {
        RUN_VARY_MSG({min_msg_size, max_msg_size}, false, [&](int msg_size, int iter) {
            int ne;
            struct ibv_wc wc;
            // wait for one recv to complete
            do {
                ne = ibv_poll_cq(device.recv_cq, 1, &wc);
                MLOG_Assert(ne >= 0, "Poll Recv CQ failed %d\n", ne);
            } while (ne == 0);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV,
                        "Recv completion failed!");
            // optionally post recv buffers
            if (--device.posted_recv_num < ibv::MIN_RECV_NUM) {
                for (int j = device.posted_recv_num; j < ibv::MAX_RECV_NUM;
                     ++j) {
                    int ret = ibv::postRecv(&device, device.mr_addr,
                                            device.mr_size, device.dev_mr->rkey,
                                            NULL);
                    MLOG_Assert(ret == 0, "Post Recv failed!");
                }
            }

            // post one send
            ibv::postSend(&device, 1 - rank, device.mr_addr, msg_size,
                          device.dev_mr->rkey, NULL);

            // wait for send to complete
            do {
                ne = ibv_poll_cq(device.send_cq, 1, &wc);
                MLOG_Assert(ne >= 0, "Poll Send CQ failed %d\n", ne);
            } while (ne == 0);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_SEND,
                        "Send completion failed!");
        });
    }

    ibv::finalize(&device);
    return 0;
}
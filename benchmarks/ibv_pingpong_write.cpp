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
    memset(device.mr_addr, 0, ibv::MR_SIZE);
    pmi_barrier();

    if (rank == 0) {
        RUN_VARY_MSG({min_msg_size, max_msg_size}, true, [&](int msg_size, int iter) {
            // post one write
          memset(device.mr_addr, 'a', msg_size);
            int ret = ibv::postWrite(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->rkey,
                                     device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, NULL);
            MLOG_Assert(ret == 0, "Post Write failed!");

            // wait for write to complete
            int ne;
            struct ibv_wc wc;
            do {
                ne = ibv_poll_cq(device.send_cq, 1, &wc);
                MLOG_Assert(ne >= 0, "Poll Send CQ failed %d\n", ne);
            } while (ne == 0);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Send completion failed!");

            // wait for remote write to complete
            volatile char *buf = (char*) device.mr_addr;
            while (!(buf[msg_size-1] == 'b' && buf[0] == 'b')) continue;
        });
    } else {
        RUN_VARY_MSG({min_msg_size, max_msg_size}, false, [&](int msg_size, int iter) {
            int ne;
            struct ibv_wc wc;
            // wait for remote write to complete
            volatile char *buf = (char*) device.mr_addr;
            while (!(buf[msg_size-1] == 'a' && buf[0] == 'a')) continue;

            // post one write
            memset(device.mr_addr, 'b', msg_size);
            int ret = ibv::postWrite(&device, 1-rank, device.mr_addr, msg_size, device.dev_mr->rkey,
                                   device.rmrs[1-rank].addr, device.rmrs[1-rank].rkey, NULL);
            MLOG_Assert(ret == 0, "Post Write failed!");

            // wait for write to complete
            do {
              ne = ibv_poll_cq(device.send_cq, 1, &wc);
              MLOG_Assert(ne >= 0, "Poll Send CQ failed %d\n", ne);
            } while (ne == 0);
            MLOG_Assert(wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RDMA_WRITE, "Send completion failed!");
        });
    }

    pmi_barrier();
    ibv::finalize(&device);
    return 0;
}
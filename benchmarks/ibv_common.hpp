//
// Created by jiakunyan on 4/20/21.
//

#ifndef IBVBENCH_IBV_COMMON_HPP
#define IBVBENCH_IBV_COMMON_HPP

#include <iostream>
#include <cassert>
#include <unistd.h>
#include "infiniband/verbs.h"
#include "mlog.h"
#include "pmi_wrapper.h"

namespace ibv {
const char *mtu_str(enum ibv_mtu mtu)
{
    switch (mtu) {
        case IBV_MTU_256:  return "256";
        case IBV_MTU_512:  return "512";
        case IBV_MTU_1024: return "1024";
        case IBV_MTU_2048: return "2048";
        case IBV_MTU_4096: return "4096";
        default:           return "invalid MTU";
    }
}
} // namespace ibv

namespace ibv {
const int MAX_SEND_NUM = 256;
const int MAX_RECV_NUM = 8;
const int MIN_RECV_NUM = 4;
const int MAX_SGE_NUM = 1;
const int MAX_CQE_NUM = MAX_RECV_NUM + 1;
const int CACHE_LINE_SIZE = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
const int PAGE_SIZE = sysconf(_SC_PAGESIZE);
const int MR_SIZE = 64 * 1024; // 64KB for now

struct RemoteMemRegion {
    uintptr_t addr;
    uint32_t size;
    uint32_t rkey;
};

struct Device {
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev;
    struct ibv_context *dev_ctx;
    struct ibv_pd* dev_pd;
    struct ibv_device_attr dev_attr;
    struct ibv_port_attr port_attr;
    struct ibv_mr * dev_mr;
    struct ibv_srq * dev_srq;
    struct ibv_cq *send_cq, *recv_cq;
    struct ibv_qp **qps;
    RemoteMemRegion *rmrs;
    void *mr_addr;
    uint32_t mr_size;
    uint8_t dev_port;
    int posted_recv_num = 0;
    // Helper fields.
    int* qp2rank;
    int qp2rank_mod;
};

int postRecv(Device *device, void *buf, uint32_t size, uint32_t lkey, void *user_context);

void init(char *devname, Device *device) {
    MLOG_Init();
    pmi_master_init();
    int rank = pmi_get_rank();
    int nranks = pmi_get_size();

    int num_devices;
    device->dev_list = ibv_get_device_list(&num_devices);
    if (num_devices <= 0) {
        fprintf(stderr, "Unable to find any IB devices\n");
        exit(EXIT_FAILURE);
    }

    if (!devname) {
        // Use the last one by default.
        device->ib_dev = device->dev_list[num_devices - 1];
        if (!device->ib_dev) {
            fprintf(stderr, "No IB devices found\n");
            exit(EXIT_FAILURE);
        }
        MLOG_Log(MLOG_LOG_INFO, "Use IB device: %s\n", ibv_get_device_name(device->ib_dev));
    } else {
        int i;
        for (i = 0; device->dev_list[i]; ++i)
            if (!strcmp(ibv_get_device_name(device->dev_list[i]), devname))
                break;
        device->ib_dev = device->dev_list[i];
        if (!device->ib_dev) {
            fprintf(stderr, "IB device %s not found\n", devname);
            exit(EXIT_FAILURE);
        }
    }

    // ibv_open_device provides the user with a verbs context which is the object that will be used for
    // all other verb operations.
    device->dev_ctx = ibv_open_device(device->ib_dev);
    if (!device->dev_ctx) {
        fprintf(stderr, "Couldn't get context for %s\n", ibv_get_device_name(device->ib_dev));
        exit(EXIT_FAILURE);
    }

    // allocate protection domain
    device->dev_pd = ibv_alloc_pd(device->dev_ctx);
    if (!device->dev_pd) {
        fprintf(stderr, "Could not create protection domain for context\n");
        exit(EXIT_FAILURE);
    }

    // query device attribute
    int rc = ibv_query_device(device->dev_ctx, &device->dev_attr);
    if (rc != 0) {
        fprintf(stderr, "Unable to query device\n");
        exit(EXIT_FAILURE);
    }

    // query port attribute
    uint8_t dev_port = 0;
    for (; dev_port < 128; dev_port++) {
        rc = ibv_query_port(device->dev_ctx, dev_port, &device->port_attr);
        if (rc == 0) {
            break;
        }
    }
    if (rc != 0) {
        fprintf(stderr, "Unable to query port\n");
        exit(EXIT_FAILURE);
    } else if (device->port_attr.link_layer != IBV_LINK_LAYER_ETHERNET &&
               !device->port_attr.lid) {
        fprintf(stderr, "Couldn't get local LID\n");
        exit(EXIT_FAILURE);
    }
    device->dev_port = dev_port;
    MLOG_Log(MLOG_LOG_INFO, "Maximum MTU: %s; Active MTU: %s\n",
             mtu_str(device->port_attr.max_mtu),
             mtu_str(device->port_attr.active_mtu));

    // Create shared-receive queue, **number here affect performance**.
    struct ibv_srq_init_attr srq_attr;
    srq_attr.srq_context = NULL;
    srq_attr.attr.max_wr = MAX_RECV_NUM;
    srq_attr.attr.max_sge = MAX_SGE_NUM;
    srq_attr.attr.srq_limit = 0;
    device->dev_srq = ibv_create_srq(device->dev_pd, &srq_attr);
    if (!device->dev_srq) {
        fprintf(stderr, "Could not create shared received queue\n");
        exit(EXIT_FAILURE);
    }

    // Create completion queues.
    device->send_cq = ibv_create_cq(device->dev_ctx, MAX_CQE_NUM, NULL, NULL, 0);
    device->recv_cq = ibv_create_cq(device->dev_ctx, MAX_CQE_NUM, NULL, NULL, 0);
    if (!device->send_cq || !device->recv_cq) {
        fprintf(stderr, "Unable to create cq\n");
        exit(EXIT_FAILURE);
    }

    // Create RDMA memory.
    int mr_flags =
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    posix_memalign(&device->mr_addr, PAGE_SIZE, MR_SIZE);
    if (!device->mr_addr) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(EXIT_FAILURE);
    }
    device->mr_size = MR_SIZE;
    device->dev_mr = ibv_reg_mr(device->dev_pd, device->mr_addr, MR_SIZE, mr_flags);
    if (!device->dev_mr) {
        fprintf(stderr, "Unable to register memory region\n");
        exit(EXIT_FAILURE);
    }
    assert(device->mr_addr == device->dev_mr->addr);

    posix_memalign((void**)&device->qps, CACHE_LINE_SIZE,
                   nranks * sizeof(struct ibv_qp*));
    posix_memalign((void**)&device->rmrs, CACHE_LINE_SIZE,
                   nranks * sizeof(struct RemoteMemRegion));

    for (int i = 0; i < nranks; i++) {
        {
            struct ibv_qp_init_attr init_attr = {
                    .send_cq = device->send_cq,
                    .recv_cq = device->recv_cq,
                    .srq = device->dev_srq,
                    .cap     = {
                            .max_send_wr  = MAX_SEND_NUM,
                            .max_recv_wr  = MAX_RECV_NUM,
                            .max_send_sge = MAX_SGE_NUM,
                            .max_recv_sge = MAX_SGE_NUM,
                            // Maximum size in bytes of inline data
                            // on the send queue
                            // don't know what this means
                            .max_inline_data = 0
                    },
                    .qp_type = IBV_QPT_RC,
                    .sq_sig_all = 0
            };
            device->qps[i] = ibv_create_qp(device->dev_pd, &init_attr);

            if (!device->qps[i])  {
                fprintf(stderr, "Couldn't create QP\n");
                exit(EXIT_FAILURE);
            }

            struct ibv_qp_attr attr;
            ibv_query_qp(device->qps[i], &attr, IBV_QP_CAP, &init_attr);
            MLOG_Log(MLOG_LOG_INFO, "Maximum inline data size is %d\n",
                     init_attr.cap.max_inline_data);
        }
        {
            // When a queue pair (QP) is newly created, it is in the RESET
            // state. The first state transition that needs to happen is to
            // bring the QP in the INIT state.
            struct ibv_qp_attr attr = {
                    .qp_state        = IBV_QPS_INIT,
                    .qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                                       IBV_ACCESS_REMOTE_READ |
                                       IBV_ACCESS_REMOTE_WRITE,
                    .pkey_index      = 0,
                    .port_num        = device->dev_port,
            };

            int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                        IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
            rc = ibv_modify_qp(device->qps[i], &attr, flags);
            if (rc != 0) {
                fprintf(stderr, "Failed to modify QP to INIT\n");
                exit(EXIT_FAILURE);
            }
        }
        // At least one receive buffer should be posted
        // before the QP can be transitioned to the RTR state
        for (int j = 0; j < MAX_RECV_NUM-1; ++j) {
            rc = postRecv(device, device->mr_addr, device->mr_size,
                          device->dev_mr->lkey, NULL);
            if (rc != 0) {
                fprintf(stderr, "Failed to post recv %d\n", j);
                exit(EXIT_FAILURE);
            }
        }
        // Use this queue pair "i" to connect to rank e.
        char ep_name[256];
        sprintf(ep_name, "%lx:%x:%x:%hx",
                (uintptr_t) device->mr_addr,
                device->dev_mr->rkey, device->qps[i]->qp_num,
                device->port_attr.lid);
        pmi_publish(rank, i, ep_name);
    }

    for (int i = 0; i < nranks; i++) {
        char ep_name[256];
        uintptr_t dest_addr;
        uint32_t dest_rkey;
        uint32_t dest_qpn;
        uint16_t dest_lid;
        pmi_getname(i, rank, ep_name);
        sscanf(ep_name, "%lx:%x:%x:%hx", &dest_addr,
               &dest_rkey, &dest_qpn, &dest_lid);
        // Once a queue pair (QP) has receive buffers posted to it, it is now
        // possible to transition the QP into the ready to receive (RTR) state.
        {
            struct ibv_qp_attr attr = {
                    .qp_state		= IBV_QPS_RTR,
                    .path_mtu		= device->port_attr.active_mtu,
                    // starting receive packet sequence number
                    // (should match remote QP's sq_psn)
                    .rq_psn			= 0,
                    .dest_qp_num	= dest_qpn,
                    // an address handle (AH) needs to be created and filled in as
                    // appropriate. Minimally, ah_attr.dlid needs to be filled in.
                    .ah_attr		= {
                            .dlid		= dest_lid,
                            .sl		= 0,
                            .src_path_bits	= 0,
                            .is_global	= 0,
                            .port_num	= device->dev_port
                    },
                    // maximum number of resources for incoming RDMA requests
                    // don't know what this is
                    .max_dest_rd_atomic	= 1,
                    // minimum RNR NAK timer (recommended value: 12)
                    .min_rnr_timer		= 12,
            };
            // should not be necessary to set these, given is_global = 0
//            memset(&attr.ah_attr.grh.dgid, 0, sizeof attr.ah_attr.grh.dgid);
//            attr.ah_attr.grh.sgid_index = -1;  // gid

            int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

            rc = ibv_modify_qp(device->qps[i], &attr, flags);
            if (rc != 0) {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(EXIT_FAILURE);
            }
        }
        // Once a queue pair (QP) has reached ready to receive (RTR) state,
        // it may then be transitioned to the ready to send (RTS) state.
        {
            struct ibv_qp_attr attr {
                    .qp_state = IBV_QPS_RTS,
                    .sq_psn = 0,
                    // number of outstanding RDMA reads and atomic operations allowed
                    .max_rd_atomic = 1,
                    .timeout = 14,
                    .retry_cnt = 7,
                    .rnr_retry = 7,
            };

            int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                        IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
            rc = ibv_modify_qp(device->qps[i], &attr, flags);
            if (rc != 0) {
                fprintf(stderr, "failed to modify QP state to RTS\n");
                exit(EXIT_FAILURE);
            }
        }
        device->rmrs[i] = RemoteMemRegion {
                .addr = dest_addr,
                .size = MR_SIZE,
                .rkey = dest_rkey
        };
    }

    int j = nranks;
    int* b;
    while (j < INT32_MAX) {
        b = (int*)calloc(j, sizeof(int));
        int i = 0;
        for (; i < nranks; i++) {
            int k = (device->qps[i]->qp_num % j);
            if (b[k]) break;
            b[k] = 1;
        }
        if (i == nranks) break;
        j++;
        free(b);
    }
    MLOG_Assert(j != INT32_MAX, "Cannot find a suitable mod to hold qp2rank map\n");
    for (int i = 0; i < nranks; i++) {
        b[device->qps[i]->qp_num % j] = i;
    }
    device->qp2rank_mod = j;
    device->qp2rank = b;
    MLOG_Log(MLOG_LOG_INFO, "qp2rank_mod is %d\n", j);

    pmi_barrier();
}

void finalize(Device *device) {
//    ibv_close_device(device->dev_ctx);
//    ibv_free_device_list(device->dev_list);
    pmi_finalize();
}

int postRecv(Device *device, void *buf, uint32_t size, uint32_t lkey, void *user_context)
{
    struct ibv_sge list = {
            .addr	= (uint64_t) buf,
            .length = size,
            .lkey	= lkey
    };
    struct ibv_recv_wr wr = {
            .wr_id	    = (uint64_t) user_context,
            .next       = NULL,
            .sg_list    = &list,
            .num_sge    = 1,
    };
    struct ibv_recv_wr *bad_wr;
    ++device->posted_recv_num;
    return ibv_post_srq_recv(device->dev_srq, &wr, &bad_wr);
}

int postSend(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey, void *user_context)
{
    struct ibv_sge list = {
            .addr	= (uint64_t) buf,
            .length = size,
            .lkey	= lkey
    };
    struct ibv_send_wr wr = {
            .wr_id	    = (uint64_t) user_context,
            .next       = NULL,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_SEND,
            .send_flags = IBV_SEND_SIGNALED,
    };
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}

int postWrite(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey,
              uintptr_t remote_addr, uint32_t rkey, void *user_context)
{
    struct ibv_sge list = {
            .addr	= (uint64_t) buf,
            .length = size,
            .lkey	= lkey
    };
    struct ibv_send_wr wr = {
            .wr_id	    = (uint64_t) user_context,
            .next       = NULL,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_RDMA_WRITE,
            .send_flags = IBV_SEND_SIGNALED,
    };
    wr.wr.rdma = {
            .remote_addr = remote_addr,
            .rkey = rkey,
    };
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}

int postWriteImm(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey,
                 uintptr_t remote_addr, uint32_t rkey, uint32_t data, void *user_context)
{
    struct ibv_sge list = {
            .addr	= (uint64_t) buf,
            .length = size,
            .lkey	= lkey
    };
    struct ibv_send_wr wr = {
            .wr_id	    = (uint64_t) user_context,
            .next       = NULL,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_RDMA_WRITE_WITH_IMM,
            .send_flags = IBV_SEND_SIGNALED,
            .imm_data   = data,
    };
    wr.wr.rdma = {
            .remote_addr = remote_addr,
            .rkey = rkey,
    };
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}
} // namespace ibv

#endif//IBVBENCH_IBV_COMMON_HPP

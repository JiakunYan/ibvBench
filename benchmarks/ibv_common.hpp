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
const int CACHE_LINE_SIZE = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
const int PAGE_SIZE = sysconf(_SC_PAGESIZE);

struct RemoteMemRegion {
    uintptr_t addr;
    uint32_t size;
    uint32_t rkey;
};

struct DeviceConfig {
    bool send_inline = true;
    int inline_size = 0;
    int max_send_num = 8;
    int max_recv_num = 8;
    int min_recv_num = 8;
    int max_sge_num = 1;
    int max_cqe_num = max_recv_num + 1;
    int mr_size = 64 * 1024; // 64KB for now
};

struct Device {
    DeviceConfig config;
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

void init(char *devname, Device *device, DeviceConfig config = DeviceConfig{}) {
    MLOG_Init();
    lcm_pm_initialize();
    int rank = lcm_pm_get_rank();
    int nranks = lcm_pm_get_size();
    device->config = config;

    int num_devices;
    device->dev_list = ibv_get_device_list(&num_devices);
    if (num_devices <= 0) {
        fprintf(stderr, "Unable to find any IB devices\n");
        exit(EXIT_FAILURE);
    }

    if (!devname) {
        // Use the first one by default.
        device->ib_dev = device->dev_list[0];
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
    memset(&srq_attr, 0, sizeof(srq_attr));
    srq_attr.srq_context = NULL;
    srq_attr.attr.max_wr = device->config.max_recv_num;
    srq_attr.attr.max_sge = device->config.max_sge_num;
    srq_attr.attr.srq_limit = 0;
    device->dev_srq = ibv_create_srq(device->dev_pd, &srq_attr);
    if (!device->dev_srq) {
        fprintf(stderr, "Could not create shared received queue\n");
        exit(EXIT_FAILURE);
    }

    // Create completion queues.
    device->send_cq = ibv_create_cq(device->dev_ctx, device->config.max_cqe_num, NULL, NULL, 0);
    device->recv_cq = ibv_create_cq(device->dev_ctx, device->config.max_cqe_num, NULL, NULL, 0);
    if (!device->send_cq || !device->recv_cq) {
        fprintf(stderr, "Unable to create cq\n");
        exit(EXIT_FAILURE);
    }

    // Create RDMA memory.
    int mr_flags =
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    posix_memalign(&device->mr_addr, PAGE_SIZE, device->config.mr_size);
    if (!device->mr_addr) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(EXIT_FAILURE);
    }
    device->mr_size = device->config.mr_size;
    device->dev_mr = ibv_reg_mr(device->dev_pd, device->mr_addr, device->config.mr_size, mr_flags);
    MLOG_Log(MLOG_LOG_INFO, "register memory: %p %lu %u %u\n",
             device->dev_mr->addr, device->dev_mr->length, device->dev_mr->lkey,
             device->dev_mr->rkey);
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
            struct ibv_qp_init_attr init_attr;
            memset(&init_attr, 0, sizeof(init_attr));
            init_attr.send_cq = device->send_cq;
            init_attr.recv_cq = device->recv_cq;
            init_attr.srq = device->dev_srq;
            init_attr.cap.max_send_wr  = device->config.max_send_num;
            init_attr.cap.max_recv_wr  = device->config.max_recv_num;
            init_attr.cap.max_send_sge = device->config.max_sge_num;
            init_attr.cap.max_recv_sge = device->config.max_sge_num;
            init_attr.cap.max_inline_data = device->config.inline_size;
            init_attr.qp_type = IBV_QPT_RC;
            init_attr.sq_sig_all = 0;
            device->qps[i] = ibv_create_qp(device->dev_pd, &init_attr);

            if (!device->qps[i])  {
                fprintf(stderr, "Couldn't create QP\n");
                exit(EXIT_FAILURE);
            }

            struct ibv_qp_attr attr;
            memset(&attr, 0, sizeof(attr));
            ibv_query_qp(device->qps[i], &attr, IBV_QP_CAP, &init_attr);
            MLOG_Assert(init_attr.cap.max_inline_data >= device->config.inline_size,
                        "Specified inline size %d is too large (maximum %d)", device->config.inline_size,
                        init_attr.cap.max_inline_data);
            if (device->config.inline_size < attr.cap.max_inline_data) {
                MLOG_Log(MLOG_LOG_INFO, "Maximum inline-size(%d) > requested inline-size(%d)\n",
                       attr.cap.max_inline_data, device->config.inline_size);
            }
        }
        MLOG_Log(MLOG_LOG_INFO, "Current inline data size is %d\n",
                 device->config.inline_size);
        {
            // When a queue pair (QP) is newly created, it is in the RESET
            // state. The first state transition that needs to happen is to
            // bring the QP in the INIT state.
            struct ibv_qp_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.qp_state        = IBV_QPS_INIT;
            attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                                   IBV_ACCESS_REMOTE_READ |
                                   IBV_ACCESS_REMOTE_WRITE;
            attr.pkey_index      = 0;
            attr.port_num        = device->dev_port;

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
//        for (int j = 0; j < device->config.max_recv_num-1 /*error without -1, don't know why*/; ++j) {
//            rc = postRecv(device, device->mr_addr, device->mr_size,
//                          device->dev_mr->lkey, NULL);
//            if (rc != 0) {
//                fprintf(stderr, "Failed to post recv %d\n", j);
//                exit(EXIT_FAILURE);
//            }
//        }
        // Use this queue pair "i" to connect to rank e.
        char key[256];
        sprintf(key, "ibvBench_%d_%d", rank, i);
        char ep_name[256];
        sprintf(ep_name, "%lx:%x:%x:%hx",
                (uintptr_t) device->mr_addr,
                device->dev_mr->rkey, device->qps[i]->qp_num,
                device->port_attr.lid);
        lcm_pm_publish(key, ep_name);
    }
    lcm_pm_barrier();

    for (int i = 0; i < nranks; i++) {
        char key[256];
        sprintf(key, "ibvBench_%d_%d", i, rank);
        char ep_name[256];
        uintptr_t dest_addr;
        uint32_t dest_rkey;
        uint32_t dest_qpn;
        uint16_t dest_lid;
        lcm_pm_getname(key, ep_name);
        sscanf(ep_name, "%lx:%x:%x:%hx", &dest_addr,
               &dest_rkey, &dest_qpn, &dest_lid);
        // Once a queue pair (QP) has receive buffers posted to it, it is now
        // possible to transition the QP into the ready to receive (RTR) state.
        {
            struct ibv_qp_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.qp_state		= IBV_QPS_RTR;
            attr.path_mtu		= device->port_attr.active_mtu;
            // starting receive packet sequence number
            // (should match remote QP's sq_psn)
            attr.rq_psn			= 0;
            attr.dest_qp_num	= dest_qpn;
            // an address handle (AH) needs to be created and filled in as
            // appropriate. Minimally; ah_attr.dlid needs to be filled in.
            attr.ah_attr.dlid		= dest_lid;
            attr.ah_attr.sl		= 0;
            attr.ah_attr.src_path_bits	= 0;
            attr.ah_attr.is_global	= 0;
            attr.ah_attr.static_rate = 0;
            attr.ah_attr.port_num	= device->dev_port;
            // maximum number of resources for incoming RDMA requests
            // don't know what this is
            attr.max_dest_rd_atomic	= 1;
            // minimum RNR NAK timer (recommended value: 12)
            attr.min_rnr_timer		= 12;
            // should not be necessary to set these, given is_global = 0
            memset(&attr.ah_attr.grh, 0, sizeof attr.ah_attr.grh);

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
            struct ibv_qp_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.qp_state = IBV_QPS_RTS;
            attr.sq_psn = 0;
            // number of outstanding RDMA reads and atomic operations allowed
            attr.max_rd_atomic = 1;
            attr.timeout = 14;
            attr.retry_cnt = 7;
            attr.rnr_retry = 7;

            int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                        IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
            rc = ibv_modify_qp(device->qps[i], &attr, flags);
            if (rc != 0) {
                fprintf(stderr, "failed to modify QP state to RTS\n");
                exit(EXIT_FAILURE);
            }
        }
        device->rmrs[i].addr = dest_addr;
        device->rmrs[i].size = device->config.mr_size;
        device->rmrs[i].rkey = dest_rkey;
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

    lcm_pm_barrier();
}

void finalize(Device *device) {
//    ibv_close_device(device->dev_ctx);
//    ibv_free_device_list(device->dev_list);
    lcm_pm_finalize();
}

inline struct ibv_wc pollCQ(struct ibv_cq *cq) {
    int ne;
    struct ibv_wc wc;
    do {
        ne = ibv_poll_cq(cq, 1, &wc);
        MLOG_Assert(ne >= 0, "Poll CQ failed %d\n", ne);
    } while (ne == 0);
    return wc;
}

inline int postRecv(Device *device, void *buf, uint32_t size, uint32_t lkey, void *user_context)
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "postRecv: %p %u %u %p\n", buf,
                 size, lkey, user_context);
    struct ibv_sge list;
    list.addr	= (uint64_t) buf;
    list.length = size;
    list.lkey	= lkey;
    struct ibv_recv_wr wr;
    wr.wr_id	    = (uint64_t) user_context;
    wr.next       = NULL;
    wr.sg_list    = &list;
    wr.num_sge    = 1;
    struct ibv_recv_wr *bad_wr;
    ++device->posted_recv_num;
    return ibv_post_srq_recv(device->dev_srq, &wr, &bad_wr);
}

inline void checkAndPostRecvs(Device *device, void *buf, uint32_t size, uint32_t lkey, void *user_context) {
    if (device->posted_recv_num < device->config.min_recv_num) {
        for (int j = device->posted_recv_num; j < device->config.max_recv_num; ++j) {
            int ret = ibv::postRecv(device, buf, size, lkey, user_context);
            MLOG_Assert(ret == 0, "Post Recv %d failed!\n", j);
        }
    }
}

inline int postSend(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey, void *user_context)
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "postSend: %d %p %u %u %p\n", rank, buf,
                 size, lkey, user_context);
    struct ibv_sge list;
    list.addr	= (uint64_t) buf;
    list.length = size;
    list.lkey	= lkey;
    struct ibv_send_wr wr;
    wr.wr_id	    = (uint64_t) user_context;
    wr.next       = NULL;
    wr.sg_list    = &list;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    if (device->config.send_inline && size <= device->config.inline_size) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}

inline int postSendImm(Device *device, int rank, void *buf, uint32_t size,
                       uint32_t lkey, uint32_t data, void *user_context)
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "postSendImm: %d %p %u %u %u %p\n", rank, buf,
                 size, lkey, data, user_context);
    struct ibv_sge list;
    list.addr	= (uint64_t) buf;
    list.length = size;
    list.lkey	= lkey;
    struct ibv_send_wr wr;
    wr.wr_id	    = (uint64_t) user_context;
    wr.next       = NULL;
    wr.sg_list    = &list;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.imm_data   = data;
    if (device->config.send_inline && size <= device->config.inline_size) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}

inline int postWrite(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey,
              uintptr_t remote_addr, uint32_t rkey, void *user_context)
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "postWrite: %d %p %u %u %p %u %p\n", rank, buf,
                 size, lkey, (void*) remote_addr, rkey, user_context);
    struct ibv_sge list;
    list.addr	= (uint64_t) buf;
    list.length = size;
    list.lkey	= lkey;
    struct ibv_send_wr wr;
    wr.wr_id	    = (uint64_t) user_context;
    wr.next       = NULL;
    wr.sg_list    = &list;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = rkey;
    if (device->config.send_inline && size <= device->config.inline_size) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}

inline int postWriteImm(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey,
                 uintptr_t remote_addr, uint32_t rkey, uint32_t data, void *user_context)
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "postWriteImm: %d %p %u %u %p %u %u %p\n", rank, buf,
                 size, lkey, (void*) remote_addr, rkey, data, user_context);
    struct ibv_sge list;
    list.addr	= (uint64_t) buf;
    list.length = size;
    list.lkey	= lkey;
    struct ibv_send_wr wr;
    wr.wr_id	    = (uint64_t) user_context;
    wr.next       = NULL;
    wr.sg_list    = &list;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.imm_data   = data;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = rkey;
    if (device->config.send_inline && size <= device->config.inline_size) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}

inline int postRead(Device *device, int rank, void *buf, uint32_t size, uint32_t lkey,
                     uintptr_t remote_addr, uint32_t rkey, void *user_context)
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "postRead: %d %p %u %u %p %u %p\n", rank, buf,
                 size, lkey, (void*) remote_addr, rkey, user_context);
    struct ibv_sge list;
    list.addr	= (uint64_t) buf;
    list.length = size;
    list.lkey	= lkey;
    struct ibv_send_wr wr;
    wr.wr_id	    = (uint64_t) user_context;
    wr.next       = NULL;
    wr.sg_list    = &list;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = rkey;
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(device->qps[rank], &wr, &bad_wr);
}
} // namespace ibv
#endif//IBVBENCH_IBV_COMMON_HPP

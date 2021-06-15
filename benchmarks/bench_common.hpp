//
// Created by jiakunyan on 1/30/21.
//

#ifndef FABRICBENCH_COMM_EXP_HPP
#define FABRICBENCH_COMM_EXP_HPP
#include <iostream>
#include <sys/time.h>
#include <getopt.h>
#include "bench_config.h"

#define LARGE 8192
#define TOTAL 4000
#define SKIP 1000
#define TOTAL_LARGE 1000
#define SKIP_LARGE 100

namespace bench {
#ifdef USE_PAPI
#include <papi.h>
int papi_events[] = { PAPI_L1_TCM, PAPI_L2_TCM, PAPI_L3_TCM };
const int PAPI_NUM = sizeof(papi_events) / sizeof(papi_events[0]);
char papi_event_names[PAPI_NUM][10] = { "L1_TCM", "L2_TCM", "L3_TCM" };

#define PAPI_SAFECALL(x)                                                    \
  {                                                                         \
    int err = (x);                                                          \
    if (err != PAPI_OK) {                                                   \
      printf("err : %d/%s (%s:%d)\n", err, PAPI_strerror(err), __FILE__, __LINE__); \
      exit(err);                                                            \
    }                                                                       \
  }                                                                         \
  while (0)                                                                 \
    ;
#else
int papi_events[] = { };
const int PAPI_NUM = 0;
char papi_event_names[PAPI_NUM][10] = { };
#define PAPI_SAFECALL(x)
#endif

void init(bool isMultithreaded = false) {
#ifdef USE_PAPI
    int retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI library init error!\n");
        exit(1);
    }
#endif
    if (isMultithreaded) {
        PAPI_SAFECALL(PAPI_thread_init(pthread_self));
    }
}

void finalize() {}

static inline double wtime() {
    timeval t1;
    gettimeofday(&t1, nullptr);
    return t1.tv_sec + t1.tv_usec / 1e6;
}

void write_buffer(char *buffer, int len, char input) {
    for (int i = 0; i < len; ++i) {
        buffer[i] = input;
    }
}

void check_buffer(const char *buffer, int len, char expect) {
    for (int i = 0; i < len; ++i) {
        if (buffer[i] != expect) {
            printf("check_buffer failed! buffer[%d](%d) != %d. ABORT!\n", i, buffer[i], expect);
            abort();
        }
    }
}

static inline double get_latency(double time, double n_msg) {
    return time / n_msg;
}

static inline double get_msgrate(double time, double n_msg) {
    return n_msg / time;
}

static inline double get_bw(double time, size_t size, double n_msg) {
    return n_msg * size / time;
}

inline void print_banner()
{
    char str[256];
    int used = 0;
    used += snprintf(str+used, 256-used, "%-10s %-10s %-10s %-10s", "Size", "us", "Mmsg/s", "MB/s");
    for (auto & papi_event_name : papi_event_names) {
        used += snprintf(str+used, 256-used, " %-10s", papi_event_name);
    }
    printf("%s\n", str);
    fflush(stdout);
}

template<typename FUNC>
static inline void RUN_VARY_MSG(std::pair<size_t, size_t> &&range,
                                const int report,
                                FUNC &&f, std::pair<int, int> &&iter = {0, 1}) {
    double t = 0;
    int loop = TOTAL;
    int skip = SKIP;
    long long state;
    long long count = 0;

#ifdef USE_PAPI
    int papi_eventSet = PAPI_NULL;
    long_long papi_values[PAPI_NUM];
    PAPI_SAFECALL(PAPI_create_eventset(&papi_eventSet));
    PAPI_SAFECALL(PAPI_add_events(papi_eventSet, papi_events, PAPI_NUM));
#endif

    for (size_t msg_size = range.first; msg_size <= range.second; msg_size <<= 1) {
        if (msg_size >= LARGE) {
            loop = TOTAL_LARGE;
            skip = SKIP_LARGE;
        }

        for (int i = iter.first; i < skip; i += iter.second) {
            f(msg_size, i);
        }

        PAPI_SAFECALL(PAPI_start(papi_eventSet));
        t = wtime();

        for (int i = iter.first; i < loop; i += iter.second) {
            f(msg_size, i);
        }

        PAPI_SAFECALL(PAPI_stop(papi_eventSet, papi_values));
        t = wtime() - t;

        if (report) {
            double latency = 1e6 * get_latency(t, 2.0 * loop); // one-way latency
            double msgrate = get_msgrate(t, loop) / 1e6;           // single-direction message rate
            double bw = get_bw(t, msg_size, loop) / 1024 / 1024;   // single-direction bandwidth

            char output_str[256];
            int used = 0;
            used += snprintf(output_str + used, 256, "%-10lu %-10.2f %-10.3f %-10.2f",
                             msg_size, latency, msgrate, bw);
#ifdef USE_PAPI
            for (long_long papi_value : papi_values) {
                double event = (double)papi_value / (2.0 * (loop / iter.second));
                used += snprintf(output_str + used, 256 - used, " %-10.2f", event);
            }
#endif
            printf("%s\n", output_str);
            fflush(stdout);
        }
    }
}

inline int comm_set_me_to(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

inline void MEM_FENCE()
{
    asm volatile("mfence" ::: "memory");
}
} // namespace fb
#endif//FABRICBENCH_COMM_EXP_HPP

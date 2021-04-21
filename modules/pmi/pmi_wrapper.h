#ifndef FABRICBENCH_PMI_WRAPPER_HPP
#define FABRICBENCH_PMI_WRAPPER_HPP

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef USE_PMI2
#include "pmi2.h"
#else
#include "pmi.h"
#endif

void pmi_master_init();
int pmi_get_rank();
int pmi_get_size();
void pmi_put(char *key, char *value);
void pmi_get(char *key, char *value);
void pmi_publish(int prank, int erank, char *value);
void pmi_getname(int prank, int erank, char *value);
void pmi_barrier();
void pmi_finalize();

#if defined(__cplusplus)
}
#endif
#endif

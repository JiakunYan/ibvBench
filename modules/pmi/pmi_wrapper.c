#include <stdio.h>
#include "pmi_wrapper.h"

static char name[256];

void pmi_master_init()
{
    int size;
    int rank;
    int spawned;
#ifdef USE_PMI2
    int appnum;
    PMI2_Init(&spawned, size, rank, &appnum);
    PMI2_Job_GetId(name, 255);
#else
    PMI_Init(&spawned, &size, &rank);
    PMI_KVS_Get_my_name(name, 255);
#endif
}

int pmi_get_rank() {
    int rank;
#ifdef USE_PMI2
    PMI2_Job_GetRank(&rank);
#else
    PMI_Get_rank(&rank);
#endif
    return rank;
}

int pmi_get_size() {
    int size;
#ifdef USE_PMI2
    PMI2_Info_GetSize(&size);
#else
    PMI_Get_size(&size);
#endif
    return size;
}

void pmi_put(char* key, char* value)
{
#ifdef USE_PMI2
    PMI2_KVS_Put(key, value);
    PMI2_KVS_Fence();
#else
    PMI_KVS_Put(name, key, value);
    PMI_Barrier();
#endif
}

void pmi_get(char* key, char* value)
{
#ifdef USE_PMI2
    int vallen;
    PMI2_KVS_Get(NULL, PMI2_ID_NULL, key, value, 255, &vallen);
#else
    PMI_KVS_Get(name, key, value, 255);
#endif
}

void pmi_publish(int rank, int gid, char* value)
{
    char key[256];
    sprintf(key, "_KEY_%d_%d", rank, gid);
    pmi_put(key, value);
}

void pmi_getname(int rank, int gid, char* value)
{
    char key[256];
    sprintf(key, "_KEY_%d_%d", rank, gid);
    pmi_get(key, value);
}

void pmi_barrier() {
#ifdef USE_PMI2
    PMI2_KVS_Fence();
#else
    PMI_Barrier();
#endif
}

void pmi_finalize() {
#ifdef USE_PMI2
    PMI2_Finalize();
#else
    PMI_Finalize();
#endif
}
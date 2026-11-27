#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static void *pin_worker(void *arg)
{
    int cpu = *(int *)arg;
    cpu_set_t set;
    cpu_set_t got;
    int rc;

    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        fprintf(stderr, "pthread_setaffinity_np(cpu=%d): %s\n", cpu, strerror(rc));
        return (void *)1;
    }

    CPU_ZERO(&got);
    rc = pthread_getaffinity_np(pthread_self(), sizeof(got), &got);
    if (rc != 0 || !CPU_ISSET(cpu, &got)) {
        fprintf(stderr, "affinity readback failed for CPU %d (rc=%d)\n", cpu, rc);
        return (void *)1;
    }
    return NULL;
}

int main(void)
{
    cpu_set_t allowed;
    pthread_t thread;
    int cpu;
    int rc;
    int found = -1;
    void *result = NULL;

    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        perror("sched_getaffinity");
        return 1;
    }
    for (cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) {
            found = cpu;
            break;
        }
    }
    if (found < 0) {
        fprintf(stderr, "no allowed CPUs in affinity mask\n");
        return 1;
    }

    rc = pthread_create(&thread, NULL, pin_worker, &found);
    if (rc != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(rc));
        return 1;
    }
    rc = pthread_join(thread, &result);
    if (rc != 0) {
        fprintf(stderr, "pthread_join: %s\n", strerror(rc));
        return 1;
    }
    if (result != NULL) {
        return 1;
    }
    printf("PASS: worker pinned to allowed logical CPU %d and readback matched\n", found);
    return 0;
}

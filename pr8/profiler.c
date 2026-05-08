/*
 * profiler.c – LD_PRELOAD library to profile function calls.
 * Compile: gcc -shared -fPIC -o profiler.so profiler.c -ldl
 * Usage:   LD_PRELOAD=./profiler.so ./instrumented_program
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

// Forward declaration
void profiler_fini(void);

// Simple hash table for function call counts
typedef struct {
    char *name;
    unsigned long long count;
    long long total_ns;
} ProfEntry;

static ProfEntry *prof_table = NULL;
static int prof_capacity = 0;
static int prof_count = 0;

static void add_profile(const char *name, long long ns) {
    for (int i = 0; i < prof_count; i++) {
        if (strcmp(prof_table[i].name, name) == 0) {
            prof_table[i].count++;
            prof_table[i].total_ns += ns;
            return;
        }
    }
    if (prof_count >= prof_capacity) {
        prof_capacity = prof_capacity ? prof_capacity * 2 : 64;
        prof_table = realloc(prof_table, prof_capacity * sizeof(ProfEntry));
    }
    prof_table[prof_count].name = strdup(name);
    prof_table[prof_count].count = 1;
    prof_table[prof_count].total_ns = ns;
    prof_count++;
}

__attribute__((constructor))
void profiler_init(void) {
    printf("[Profiler] Initialized. Will collect function call data.\n");
    atexit(profiler_fini);
}

__attribute__((destructor))
void profiler_fini(void) {
    printf("\n[Profiler] === Function Call Profile ===\n");
    printf("%-30s %10s %15s %15s\n", "Function", "Calls", "Total time (ns)", "Avg (ns)");
    for (int i = 0; i < prof_count; i++) {
        long long avg = prof_table[i].total_ns / (prof_table[i].count ? prof_table[i].count : 1);
        printf("%-30s %10llu %15lld %15lld\n",
               prof_table[i].name, prof_table[i].count,
               prof_table[i].total_ns, avg);
    }
}

// Example wrapper for a function named "add"
int add(int a, int b) {
    static int (*real_add)(int, int) = NULL;
    if (!real_add) real_add = dlsym(RTLD_NEXT, "add");
    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    int ret = real_add(a, b);
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    long long ns = (ts_end.tv_sec - ts_start.tv_sec) * 1000000000LL +
                   (ts_end.tv_nsec - ts_start.tv_nsec);
    add_profile("add", ns);
    return ret;
}

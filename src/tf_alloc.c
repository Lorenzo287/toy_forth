#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef STB_LEAKCHECK
#define STB_LEAKCHECK_IMPLEMENTATION
#endif

#include "tf_alloc.h"

#ifdef TF_ALLOC_STATS
#define TF_ALLOC_SITE_CAPACITY 256

typedef enum {
    TF_ALLOC_MALLOC,
    TF_ALLOC_CALLOC,
    TF_ALLOC_REALLOC,
} tf_alloc_kind;

typedef struct {
    const char *file;
    int line;
    size_t malloc_calls;
    size_t calloc_calls;
    size_t realloc_calls;
    size_t requested_bytes;
} tf_alloc_site;

static size_t malloc_calls = 0;
static size_t calloc_calls = 0;
static size_t realloc_calls = 0;
static size_t requested_bytes = 0;
static size_t unattributed_calls = 0;
static size_t unattributed_bytes = 0;
static tf_alloc_site allocation_sites[TF_ALLOC_SITE_CAPACITY] = {0};

static size_t allocation_site_hash(const char *file, int line) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char *p = (const unsigned char *)file; *p; p++) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    hash ^= (unsigned)line;
    hash *= UINT64_C(1099511628211);
    return (size_t)(hash ^ (hash >> 32));
}

static tf_alloc_site *find_allocation_site(const char *file, int line) {
    size_t index = allocation_site_hash(file, line) &
                   (TF_ALLOC_SITE_CAPACITY - 1);
    for (size_t probe = 0; probe < TF_ALLOC_SITE_CAPACITY; probe++) {
        tf_alloc_site *site = &allocation_sites[index];
        if (!site->file) {
            site->file = file;
            site->line = line;
            return site;
        }
        if (site->line == line && strcmp(site->file, file) == 0) return site;
        index = (index + 1) & (TF_ALLOC_SITE_CAPACITY - 1);
    }
    return NULL;
}

static void record_allocation(tf_alloc_kind kind, size_t size,
                              const char *file, int line) {
    switch (kind) {
    case TF_ALLOC_MALLOC: malloc_calls++; break;
    case TF_ALLOC_CALLOC: calloc_calls++; break;
    case TF_ALLOC_REALLOC: realloc_calls++; break;
    }
    requested_bytes += size;

    tf_alloc_site *site = find_allocation_site(file, line);
    if (!site) {
        unattributed_calls++;
        unattributed_bytes += size;
        return;
    }
    switch (kind) {
    case TF_ALLOC_MALLOC: site->malloc_calls++; break;
    case TF_ALLOC_CALLOC: site->calloc_calls++; break;
    case TF_ALLOC_REALLOC: site->realloc_calls++; break;
    }
    site->requested_bytes += size;
}
#endif

#ifdef TF_ALLOC_STATS
void *tf_xmalloc_at(size_t size, const char *file, int line) {
    record_allocation(TF_ALLOC_MALLOC, size, file, line);
#else
void *tf_xmalloc(size_t size) {
#endif
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

#ifdef TF_ALLOC_STATS
void *tf_xrealloc_at(void *ptr, size_t size, const char *file, int line) {
    record_allocation(TF_ALLOC_REALLOC, size, file, line);
#else
void *tf_xrealloc(void *ptr, size_t size) {
#endif
    ptr = realloc(ptr, size);
    if (!ptr) {
        fprintf(stderr, "Out of memory reallocating %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

#ifdef TF_ALLOC_STATS
void *tf_xcalloc_at(size_t nmemb, size_t size, const char *file, int line) {
    record_allocation(TF_ALLOC_CALLOC, nmemb * size, file, line);
#else
void *tf_xcalloc(size_t nmemb, size_t size) {
#endif
#ifdef STB_LEAKCHECK
    void *ptr = malloc(nmemb * size);
    if (!ptr) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", nmemb * size);
        exit(EXIT_FAILURE);
    }
    memset(ptr, 0, nmemb * size);
    return ptr;
#else
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", nmemb * size);
        exit(EXIT_FAILURE);
    }
    return ptr;
#endif
}

#ifdef TF_ALLOC_STATS
char *tf_xstrdup_at(const char *s, const char *file, int line) {
#else
char *tf_xstrdup(const char *s) {
#endif
    size_t len = strlen(s);
#ifdef TF_ALLOC_STATS
    char *ptr = tf_xmalloc_at(len + 1, file, line);
#else
    char *ptr = tf_xmalloc(len + 1);
#endif
    memcpy(ptr, s, len + 1);
    return ptr;
}

#ifdef TF_ALLOC_STATS
static size_t allocation_site_calls(const tf_alloc_site *site) {
    return site->malloc_calls + site->calloc_calls + site->realloc_calls;
}

static int allocation_site_precedes(const tf_alloc_site *left,
                                    const tf_alloc_site *right) {
    if (left->requested_bytes != right->requested_bytes) {
        return left->requested_bytes > right->requested_bytes;
    }
    size_t left_calls = allocation_site_calls(left);
    size_t right_calls = allocation_site_calls(right);
    if (left_calls != right_calls) return left_calls > right_calls;
    int file_order = strcmp(left->file, right->file);
    if (file_order != 0) return file_order < 0;
    return left->line < right->line;
}

void tf_alloc_stats_dump(void) {
    size_t total_calls = malloc_calls + calloc_calls + realloc_calls;
    fprintf(stderr,
            "allocations: %zu (malloc %zu, calloc %zu, realloc %zu)\n"
            "requested bytes: %zu\n"
            "allocation sites:\n",
            total_calls, malloc_calls, calloc_calls, realloc_calls,
            requested_bytes);

    tf_alloc_site *ordered[TF_ALLOC_SITE_CAPACITY];
    size_t count = 0;
    for (size_t i = 0; i < TF_ALLOC_SITE_CAPACITY; i++) {
        if (allocation_sites[i].file) ordered[count++] = &allocation_sites[i];
    }
    for (size_t i = 1; i < count; i++) {
        tf_alloc_site *site = ordered[i];
        size_t j = i;
        while (j > 0 && allocation_site_precedes(site, ordered[j - 1])) {
            ordered[j] = ordered[j - 1];
            j--;
        }
        ordered[j] = site;
    }
    for (size_t i = 0; i < count; i++) {
        const tf_alloc_site *site = ordered[i];
        fprintf(stderr,
                "  calls=%zu bytes=%zu malloc=%zu calloc=%zu realloc=%zu "
                "at %s:%d\n",
                allocation_site_calls(site), site->requested_bytes,
                site->malloc_calls, site->calloc_calls, site->realloc_calls,
                site->file, site->line);
    }
    if (unattributed_calls > 0) {
        fprintf(stderr,
                "  unattributed calls=%zu bytes=%zu (site table full)\n",
                unattributed_calls, unattributed_bytes);
    }
}
#endif

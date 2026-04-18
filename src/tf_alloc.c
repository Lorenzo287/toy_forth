#include <stdio.h>
#include <stdlib.h>

#ifdef STB_LEAKCHECK
    #define STB_LEAKCHECK_IMPLEMENTATION
    #include <string.h>
#endif

#include "tf_alloc.h"
#include "tf_obj.h"

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
        exit(TF_ERR);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    ptr = realloc(ptr, size);
    if (!ptr) {
        fprintf(stderr, "Out of memory reallocating %zu bytes\n", size);
        exit(TF_ERR);
    }
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size) {
#ifdef STB_LEAKCHECK
    void *ptr = xmalloc(nmemb * size);
    memset(ptr, 0, nmemb * size);
    return ptr;
#else
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", nmemb * size);
        exit(TF_ERR);
    }
    return ptr;
#endif
}

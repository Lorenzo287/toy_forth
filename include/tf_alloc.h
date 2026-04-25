#ifndef TF_ALLOC_H
#define TF_ALLOC_H

#include <stddef.h>

#ifdef STB_LEAKCHECK
    #include "stb_leakcheck/stb_leakcheck.h"
#endif

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);

#endif  // TF_ALLOC_H

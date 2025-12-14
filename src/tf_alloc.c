#include "tf_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

char *xstrdup(char *str) {
    char *ptr = strdup(str);
    if (!ptr) {
        fprintf(stderr, "Out of memory duplicating string\n");
        exit(TF_ERR);
    }
    return ptr;
}

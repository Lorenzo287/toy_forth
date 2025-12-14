#ifndef TF_ALLOC_H
#define TF_ALLOC_H

#include <stddef.h>

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(char *ptr);

#endif  // TF_ALLOC_H

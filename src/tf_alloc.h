#ifndef TF_ALLOC_H
#define TF_ALLOC_H

#include <stddef.h>

#ifdef STB_LEAKCHECK
#include "stb_leakcheck/stb_leakcheck.h"
#endif

/* Checked allocation helpers. They terminate the process on allocation failure. */
#ifdef TF_ALLOC_STATS
void *tf_xmalloc_at(size_t size, const char *file, int line);
void *tf_xrealloc_at(void *ptr, size_t size, const char *file, int line);
void *tf_xcalloc_at(size_t nmemb, size_t size, const char *file, int line);
char *tf_xstrdup_at(const char *s, const char *file, int line);

#define tf_xmalloc(size) tf_xmalloc_at((size), __FILE__, __LINE__)
#define tf_xrealloc(ptr, size) \
    tf_xrealloc_at((ptr), (size), __FILE__, __LINE__)
#define tf_xcalloc(nmemb, size) \
    tf_xcalloc_at((nmemb), (size), __FILE__, __LINE__)
#define tf_xstrdup(s) tf_xstrdup_at((s), __FILE__, __LINE__)
#else
void *tf_xmalloc(size_t size);
void *tf_xrealloc(void *ptr, size_t size);
void *tf_xcalloc(size_t nmemb, size_t size);
char *tf_xstrdup(const char *s);
#endif

#ifdef TF_ALLOC_STATS
void tf_alloc_stats_dump(void);
#endif

#endif  // TF_ALLOC_H

#ifndef TF_DOCS_H
#define TF_DOCS_H

#include <stddef.h>

typedef struct {
    const char *name;
    const char *stack_effect;
    const char *syntax;
    const char *description;
} tf_doc_entry;

typedef struct {
    const char *locator;
    const char *name;
    const tf_doc_entry *words;
    size_t word_count;
} tf_package_doc;

const tf_doc_entry *tf_doc_lookup(const char *name);
const tf_doc_entry *tf_doc_entries(size_t *count);
const tf_package_doc *tf_core_package_doc_lookup(const char *locator);
const tf_package_doc *tf_core_package_docs(size_t *count);

#endif  // TF_DOCS_H

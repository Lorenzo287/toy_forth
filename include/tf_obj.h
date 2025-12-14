#ifndef TF_OBJ_H
#define TF_OBJ_H

#include <stdbool.h>
#include <stddef.h>

#define TF_OK 0
#define TF_ERR 1

/* === Types Definition === */

typedef enum {
    TF_OBJ_TYPE_BOOL,
    TF_OBJ_TYPE_INT,
    TF_OBJ_TYPE_FLOAT,
    TF_OBJ_TYPE_STR,
    TF_OBJ_TYPE_SYMBOL,
    TF_OBJ_TYPE_LIST,
    TF_OBJ_TYPE_ALL = 255  // used by pop_obj
} tf_type;

// TODO: add support for variables with $ syntax,
// 		 add quoted expression for user func definition

/* === Object Definition === */

typedef struct tf_obj {
    int refcount;
    tf_type type;
    union {
        int i;
        float f;
        bool b;
        struct {
            char *ptr;
            size_t len;
            bool quoted;  // TODO:
        } str;
        struct {
            struct tf_obj **elem;
            size_t len;
            size_t cap;
        } list;
    };
} tf_obj;

/* === Object Functions === */

tf_obj *init_obj(int type);
tf_obj *init_list_obj(void);
tf_obj *create_int_obj(int i);
tf_obj *create_bool_obj(bool b);
tf_obj *create_float_obj(float f);
tf_obj *create_symbol_obj(char *s, size_t len);
tf_obj *create_string_obj(char *s, size_t len);

int compare_string_obj(tf_obj *a, tf_obj *b);
void push_obj(tf_obj *l, tf_obj *elem);
tf_obj *pop_obj_type(tf_obj *l, tf_type type);
tf_obj *pop_obj(tf_obj *l);
void retain_obj(tf_obj *o);
void release_obj(tf_obj *o);
void free_obj(tf_obj *o);
void print_obj(tf_obj *o);

#endif  // TF_OBJ_H

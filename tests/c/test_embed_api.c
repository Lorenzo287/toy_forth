#include "toy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef STB_LEAKCHECK
#include "tf_alloc.h"
#endif

#define CHECK(condition, message)                                             \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "embed API check failed: %s\n", message);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

typedef struct {
    char data[4096];
    size_t length;
} capture_buffer;

static void capture_write(void *userdata, const char *data, size_t length) {
    capture_buffer *buffer = userdata;
    size_t available = sizeof(buffer->data) - buffer->length;
    if (length > available) length = available;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
}

static void capture_clear(capture_buffer *buffer) {
    buffer->length = 0;
}

static bool capture_equals(capture_buffer *buffer, const char *expected,
                           size_t length) {
    return buffer->length == length &&
           memcmp(buffer->data, expected, length) == 0;
}

static bool capture_contains(capture_buffer *buffer, const char *expected) {
    size_t expected_len = strlen(expected);
    if (expected_len > buffer->length) return false;
    for (size_t i = 0; i <= buffer->length - expected_len; i++) {
        if (memcmp(buffer->data + i, expected, expected_len) == 0) return true;
    }
    return false;
}

static toy_status host_double(toy_state *state) {
    int64_t value = 0;
    if (!toy_get_int(state, 0, &value)) {
        return toy_fail(state, "host.double expected an integer");
    }
    if (!toy_pop(state, 1)) {
        return toy_fail(state, "host.double pop failed");
    }
    return toy_push_int(state, value * 2);
}

static void destroy_test_resource(void *resource, void *userdata) {
    int *destroy_count = userdata;
    (*destroy_count)++;
    free(resource);
}

static int failed_resource_destroy_count = 0;

static toy_status host_fail_resource(toy_state *state) {
    int *value = malloc(sizeof(*value));
    if (!value) return toy_fail(state, "resource allocation failed");
    *value = 99;
    toy_status status = toy_push_resource(
        state, "host.failed", value, destroy_test_resource,
        &failed_resource_destroy_count);
    if (status != TOY_OK) {
        free(value);
        return status;
    }
    return toy_fail(state, "failure after creating a resource");
}

static toy_status host_interrupt_now(toy_state *state) {
    toy_interrupt(state);
    return TOY_OK;
}

static const toy_native_word host_words[] = {
    {
        .name = "double",
        .callback = host_double,
        .stack_effect = "n -- 2n",
        .description = "Double an integer supplied by the embedding host.",
    },
    {.name = "fail-resource", .callback = host_fail_resource},
    {.name = "interrupt", .callback = host_interrupt_now},
};

static const toy_native_package host_package = {
    "host",
    host_words,
    sizeof(host_words) / sizeof(host_words[0]),
};

static const toy_native_word host_tools_words[] = {
    {.name = "double", .callback = host_double},
};

static const toy_native_package host_tools_package = {
    "tools",
    host_tools_words,
    sizeof(host_tools_words) / sizeof(host_tools_words[0]),
};

static const toy_native_word invalid_atomic_words[] = {
    {.name = "ok", .callback = host_double},
    {.name = "bad.name", .callback = host_double},
};

static const toy_native_package invalid_atomic_package = {
    "atomic",
    invalid_atomic_words,
    sizeof(invalid_atomic_words) / sizeof(invalid_atomic_words[0]),
};

static const toy_native_word atomic_words[] = {
    {.name = "ok", .callback = host_double},
};

static const toy_native_package atomic_package = {
    "atomic",
    atomic_words,
    sizeof(atomic_words) / sizeof(atomic_words[0]),
};

static bool stack_is_single_int(toy_state *state, int64_t expected) {
    int64_t actual = 0;
    return toy_stack_size(state) == 1 &&
           toy_get_int(state, 0, &actual) && actual == expected;
}

int main(void) {
    capture_buffer first_output = {0};
    capture_buffer first_diagnostic = {0};
    capture_buffer second_output = {0};
    capture_buffer second_diagnostic = {0};
    toy_state_config first_config = {
        .output = capture_write,
        .output_userdata = &first_output,
        .diagnostic = capture_write,
        .diagnostic_userdata = &first_diagnostic,
    };
    toy_state_config second_config = {
        .output = capture_write,
        .output_userdata = &second_output,
        .diagnostic = capture_write,
        .diagnostic_userdata = &second_diagnostic,
    };
    toy_state *first = toy_state_new(&first_config);
    toy_state *second = toy_state_new(&second_config);
    CHECK(first && second, "state creation");

    CHECK(toy_eval(first, "<output>",
                   "\"hello\" print 4 5 \"{}+{}\" printf "
                   "1 \"two\" .s .S") == TOY_OK,
          "capture language output");
    const char expected_output[] =
        "hello\n4+5<2> 1 two \n<2> 1 \"two\" \n";
    CHECK(capture_equals(&first_output, expected_output,
                         sizeof(expected_output) - 1),
          "captured print, printf, and stack output");
    CHECK(first_diagnostic.length == 0,
          "ordinary output did not use diagnostic callback");
    CHECK(second_output.length == 0, "output remains state-local");
    CHECK(toy_pop(first, 2), "clear output test stack");

    capture_clear(&first_output);
    CHECK(toy_eval(first, "<binary-output>", "\"a\\x00b\" print") ==
              TOY_OK,
          "capture binary-safe output");
    const char expected_binary[] = {'a', '\0', 'b', '\n'};
    CHECK(capture_equals(&first_output, expected_binary,
                         sizeof(expected_binary)),
          "output callback receives explicit byte lengths");
    capture_clear(&first_output);

    CHECK(toy_eval(first, "<arithmetic>", "2 3 +") == TOY_OK,
          "evaluate arithmetic");
    int64_t integer = 0;
    CHECK(toy_stack_size(first) == 1, "arithmetic stack size");
    CHECK(toy_stack_type(first, 0) == TOY_TYPE_INT, "arithmetic result type");
    CHECK(toy_get_int(first, 0, &integer) && integer == 5,
          "arithmetic result value");
    CHECK(toy_pop(first, 1), "pop arithmetic result");

    CHECK(toy_push_int(first, INT64_MAX) == TOY_OK,
          "push boxed-fallback integer");
    toy_value *wide_integer = toy_value_retain(first, 0);
    CHECK(wide_integer && toy_value_type(wide_integer) == TOY_TYPE_INT &&
              toy_value_get_int(wide_integer, &integer) &&
              integer == INT64_MAX,
          "retain boxed-fallback integer");
    CHECK(toy_pop(first, 1), "pop boxed-fallback integer");
    toy_value_release(wide_integer);

    CHECK(toy_push_int(first, 42) == TOY_OK, "push immediate integer");
    toy_value *immediate_integer = toy_value_retain(first, 0);
    CHECK(immediate_integer && toy_value_type(immediate_integer) == TOY_TYPE_INT &&
              toy_value_get_int(immediate_integer, &integer) && integer == 42,
          "retain immediate integer");
    CHECK(toy_pop(first, 1), "pop immediate integer");
    toy_value_release(immediate_integer);

    CHECK(toy_register_package(first, &host_package) == TOY_OK,
          "register package");
    CHECK(toy_eval(first, "<native-doc>", "'host.double doc") == TOY_OK,
          "read registered native word documentation");
    const char expected_native_doc[] =
        "host.double\nstack: n -- 2n\n"
        "Double an integer supplied by the embedding host.";
    const char *native_doc = NULL;
    size_t native_doc_length = 0;
    CHECK(toy_get_string(first, 0, &native_doc, &native_doc_length) &&
              native_doc_length == sizeof(expected_native_doc) - 1 &&
              memcmp(native_doc, expected_native_doc, native_doc_length) == 0,
          "registered native word documentation");
    CHECK(toy_pop(first, 1), "pop registered native word documentation");
    CHECK(toy_eval(first, "<native>", "21 host.double") == TOY_OK,
          "call registered package");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "native word result");
    CHECK(toy_pop(first, 1), "pop native result");
    CHECK(toy_register_package(first, &host_tools_package) == TOY_OK,
          "register second package");
    CHECK(toy_eval(first, "<second-native>", "21 tools.double") == TOY_OK,
          "call second package");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "second package result");
    CHECK(toy_pop(first, 1), "pop second package result");
    CHECK(toy_eval(first, "<native-error>", "\"bad\" host.double") ==
              TOY_ERROR,
          "native word error status");
    CHECK(toy_get_error(first) &&
              strcmp(toy_get_error(first),
                     "host.double expected an integer") == 0,
          "native word diagnostic");
    CHECK(toy_pop(first, 1), "pop rejected native input");

    CHECK(toy_register_package(first, &host_package) == TOY_ERROR,
          "reject duplicate package");
    CHECK(toy_get_error(first) &&
              strstr(toy_get_error(first), "already registered"),
          "duplicate package diagnostic");
    CHECK(toy_register_word(first, "host.extra", host_double) == TOY_ERROR,
          "standalone registration requires a local name");
    CHECK(toy_get_error(first) &&
              strstr(toy_get_error(first), "invalid standalone"),
          "standalone native word diagnostic");

    CHECK(toy_register_package(first, &invalid_atomic_package) == TOY_ERROR,
          "reject invalid package atomically");
    CHECK(toy_get_error(first) &&
              strstr(toy_get_error(first), "invalid native word name"),
          "invalid native word diagnostic");
    CHECK(toy_call(first, "atomic.ok") == TOY_ERROR,
          "invalid package left no partial word");
    CHECK(toy_register_package(first, &atomic_package) == TOY_OK,
          "invalid package left no registry entry");
    CHECK(toy_eval(first, "<atomic-native>", "21 atomic.ok") == TOY_OK,
          "call package after atomic retry");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "atomic retry result");
    CHECK(toy_pop(first, 1), "pop atomic retry result");

    char copied_package_name[] = "copied";
    char copied_word_name[] = "double";
    char copied_stack_effect[] = "n -- 2n";
    char copied_description[] = "Documentation with transient storage.";
    toy_native_word copied_words[] = {
        {
            .name = copied_word_name,
            .callback = host_double,
            .stack_effect = copied_stack_effect,
            .description = copied_description,
        },
    };
    toy_native_package copied_package = {
        copied_package_name,
        copied_words,
        sizeof(copied_words) / sizeof(copied_words[0]),
    };
    CHECK(toy_register_package(first, &copied_package) == TOY_OK,
          "register package with transient names");
    copied_package_name[0] = 'X';
    copied_word_name[0] = 'X';
    copied_stack_effect[0] = 'X';
    copied_description[0] = 'X';
    CHECK(toy_eval(first, "<copied-native>", "21 copied.double") == TOY_OK,
          "package copied descriptor names");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "copied package result");
    CHECK(toy_pop(first, 1), "pop copied package result");
    CHECK(toy_eval(first, "<copied-native-doc>", "'copied.double doc") ==
              TOY_OK,
          "read copied native word documentation");
    const char expected_copied_doc[] =
        "copied.double\nstack: n -- 2n\n"
        "Documentation with transient storage.";
    CHECK(toy_get_string(first, 0, &native_doc, &native_doc_length) &&
              native_doc_length == sizeof(expected_copied_doc) - 1 &&
              memcmp(native_doc, expected_copied_doc, native_doc_length) == 0,
          "package copied descriptor documentation");
    CHECK(toy_pop(first, 1), "pop copied native word documentation");

    CHECK(toy_register_word(first, "legacy-double", host_double) == TOY_OK,
          "register standalone native word");
    CHECK(toy_eval(first, "<standalone-native>", "21 legacy-double") ==
              TOY_OK,
          "call standalone native word");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "standalone native word result");
    CHECK(toy_pop(first, 1), "pop standalone native result");

    CHECK(toy_eval(first, "<definition>", "'update [ 1 + ] def") == TOY_OK,
          "define Toy word");
    CHECK(toy_push_int(first, 41) == TOY_OK, "push host argument");
    CHECK(toy_call(first, "update") == TOY_OK, "call Toy word from host");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "host call result");
    CHECK(toy_pop(first, 1), "pop host call result");

    CHECK(toy_call(second, "update") == TOY_ERROR,
          "definitions remain state-local");
    CHECK(toy_get_error(second) &&
              strstr(toy_get_error(second), "undefined word 'update'"),
          "undefined-word diagnostic");
    CHECK(toy_call(second, "host.double") == TOY_ERROR,
          "registered packages remain state-local");
    CHECK(second_diagnostic.length > 0,
          "second state receives its own diagnostics");

    CHECK(toy_eval(first, "<failure>", "1 0 /") == TOY_ERROR,
          "runtime failure status");
    CHECK(toy_get_error(first) && strstr(toy_get_error(first), "divide by zero"),
          "runtime failure diagnostic");
    CHECK(capture_contains(&first_diagnostic, "runtime error:") &&
              capture_contains(&first_diagnostic, "<failure>:1:5"),
          "runtime diagnostic callback");
    CHECK(toy_pop(first, 2), "clear failed operation inputs");
    CHECK(toy_eval(first, "<recovery>", "4 5 +") == TOY_OK,
          "state reuse after failure");
    CHECK(toy_get_int(first, 0, &integer) && integer == 9,
          "recovery result");
    CHECK(toy_pop(first, 1), "pop recovery result");

    struct {
        const char *name;
        const char *source;
    } unhandled_cases[] = {
        {"dip unwind", "1 2 [ drop 9 \"dip failure\" error ] dip"},
        {"keep unwind",
         "1 2 [ drop drop 9 \"keep failure\" error ] keep"},
        {"fold unwind",
         "1 0 [ 1 ] [ drop drop drop 9 \"fold failure\" error ] fold"},
        {"bi unwind",
         "1 2 [ drop drop 9 \"bi failure\" error ] [] bi"},
        {"app2 unwind",
         "1 2 3 [ drop drop 9 \"app2 failure\" error ] app2"},
        {"app2 result error", "1 2 3 [ drop drop 9 ] app2"},
        {"map unwind",
         "1 [ 2 ] [ drop drop 9 \"map failure\" error ] map"},
        {"replicate unwind",
         "1 1 [ drop 9 \"replicate failure\" error ] replicate"},
        {"infra unwind",
         "1 [ 2 ] [ drop 9 \"infra failure\" error ] infra"},
        {"cleave unwind",
         "1 2 [ [ drop drop 9 \"cleave failure\" error ] ] cleave"},
        {"construct unwind",
         "1 2 [ [ drop drop 9 \"construct failure\" error ] ] construct"},
        {"if predicate unwind",
         "1 [ drop 9 \"if failure\" error ] [] if"},
        {"predicate result error", "1 [ drop 9 ] [] if"},
        {"while predicate unwind",
         "1 [ drop 9 \"while failure\" error ] [] while"},
        {"cond predicate unwind",
         "1 [ [ [ drop 9 \"cond failure\" error ] [] ] ] cond"},
        {"filter predicate unwind",
         "1 [ 2 ] [ drop drop 9 \"filter failure\" error ] filter"},
        {"binrec unwind",
         "1 1 [ 0 == ] [ drop drop 9 \"binrec failure\" error ] "
         "[ pred dup ] [ + ] binrec"},
        {"treerec unwind",
         "1 2 [ drop drop 9 \"treerec failure\" error ] [] treerec"},
        {"try handler unwind",
         "1 [ drop \"body failure\" error ] "
         "[ drop drop 9 \"handler failure\" error ] try"},
    };
    for (size_t i = 0;
         i < sizeof(unhandled_cases) / sizeof(unhandled_cases[0]); i++) {
        capture_clear(&first_diagnostic);
        CHECK(toy_eval(first, unhandled_cases[i].name,
                       unhandled_cases[i].source) == TOY_ERROR,
              unhandled_cases[i].name);
        CHECK(stack_is_single_int(first, 9), unhandled_cases[i].name);
        CHECK(toy_pop(first, 1), unhandled_cases[i].name);
    }

    CHECK(toy_eval(first, "try recovery",
                   "1 [ drop 9 \"caught failure\" error ] "
                   "[ drop 7 ] try") == TOY_OK,
          "try recovery status");
    CHECK(toy_stack_size(first) == 2 &&
              toy_get_int(first, 0, &integer) && integer == 7 &&
              toy_get_int(first, 1, &integer) && integer == 1,
          "try is the explicit stack-restoring boundary");
    CHECK(toy_pop(first, 2), "pop try recovery results");

    CHECK(toy_eval(first, "try interruption",
                   "1 [ drop 9 host.interrupt ] [] try") == TOY_INTERRUPTED,
          "try does not catch interruption");
    CHECK(stack_is_single_int(first, 9),
          "interruption does not roll back completed stack effects");
    CHECK(toy_pop(first, 1), "pop interrupted try stack");

    CHECK(toy_eval(first, "try exit", "1 [ drop 9 exit ] [] try") ==
              TOY_EXIT_REQUESTED,
          "try does not catch exit");
    CHECK(stack_is_single_int(first, 9),
          "exit does not roll back completed stack effects");
    CHECK(toy_pop(first, 1), "pop exited try stack");

    CHECK(toy_eval(first, "<exit>", "exit") == TOY_EXIT_REQUESTED,
          "embedded exit request");
    CHECK(toy_eval(first, "<after-exit>", "6 7 +") == TOY_OK,
          "state survives exit request");
    CHECK(toy_get_int(first, 0, &integer) && integer == 13,
          "post-exit result");
    CHECK(toy_pop(first, 1), "pop post-exit result");

    toy_interrupt(first);
    CHECK(toy_eval(first, "<interrupt>", "99") == TOY_INTERRUPTED,
          "per-state interrupt request");
    CHECK(toy_stack_size(first) == 0, "interrupted source did not execute");
    CHECK(toy_eval(first, "<after-interrupt>", "8 9 +") == TOY_OK,
          "state survives interrupt request");
    CHECK(toy_get_int(first, 0, &integer) && integer == 17,
          "post-interrupt result");
    CHECK(toy_pop(first, 1), "pop post-interrupt result");

    capture_clear(&first_diagnostic);
    CHECK(toy_eval(first, "<parse-error>", "[") == TOY_ERROR,
          "parse failure status");
    CHECK(toy_get_error(first) &&
              strcmp(toy_get_error(first),
                     "expected ']' but reached end of program at "
                     "<parse-error>:1:2") == 0,
          "parse failure diagnostic");
    const char expected_parse_diagnostic[] =
        "parsing error: expected ']' but reached end of program\n"
        "  at <parse-error>:1:2\n";
    CHECK(capture_equals(&first_diagnostic, expected_parse_diagnostic,
                         sizeof(expected_parse_diagnostic) - 1),
          "detailed parser diagnostic callback");

    CHECK(toy_push_bool(first, true) == TOY_OK, "push bool");
    bool boolean = false;
    CHECK(toy_get_bool(first, 0, &boolean) && boolean, "get bool");
    toy_value *bool_value = toy_value_retain(first, 0);
    CHECK(bool_value && toy_value_type(bool_value) == TOY_TYPE_BOOL &&
              toy_value_get_bool(bool_value, &boolean) && boolean,
          "retain and inspect bool");
    toy_value_release(bool_value);
    CHECK(toy_pop(first, 1), "pop bool");

    CHECK(toy_push_float(first, 2.5) == TOY_OK, "push float");
    double floating = 0.0;
    CHECK(toy_get_float(first, 0, &floating) && floating == 2.5, "get float");
    toy_value *float_value = toy_value_retain(first, 0);
    CHECK(float_value && toy_value_get_float(float_value, &floating) &&
              floating == 2.5,
          "retain and inspect float");
    toy_value_release(float_value);
    CHECK(toy_pop(first, 1), "pop float");

    CHECK(toy_push_string(first, "hello", 5) == TOY_OK, "push string");
    const char *text = NULL;
    size_t length = 0;
    CHECK(toy_get_string(first, 0, &text, &length) && length == 5 &&
              memcmp(text, "hello", 5) == 0,
          "get string");
    toy_value *string_value = toy_value_retain(first, 0);
    CHECK(string_value &&
              toy_value_get_string(string_value, &text, &length) &&
              length == 5 && memcmp(text, "hello", 5) == 0,
          "retain and inspect string");
    size_t sequence_size = 0;
    CHECK(toy_sequence_size(string_value, &sequence_size) &&
              sequence_size == 5,
          "string sequence size");
    toy_value *string_item = toy_sequence_get(string_value, 1);
    CHECK(string_item &&
              toy_value_get_string(string_item, &text, &length) &&
              length == 1 && text[0] == 'e',
          "string sequence item");
    toy_value_release(string_item);
    toy_value_release(string_value);
    CHECK(toy_pop(first, 1), "pop string");

    CHECK(toy_push_int(first, 3) == TOY_OK &&
              toy_push_int(first, 4) == TOY_OK &&
              toy_push_int(first, 5) == TOY_OK,
          "push vector items");
    CHECK(toy_make_vector(first, 3) == TOY_OK, "make vector from stack");
    toy_value *scores = toy_value_retain(first, 0);
    CHECK(scores && toy_value_type(scores) == TOY_TYPE_VECTOR &&
              toy_sequence_size(scores, &sequence_size) &&
              sequence_size == 3,
          "retain constructed vector");
    toy_value *score = toy_sequence_get(scores, 1);
    CHECK(score && toy_value_get_int(score, &integer) && integer == 4,
          "inspect constructed vector item");
    toy_value_release(score);
    CHECK(!toy_sequence_get(scores, 3), "reject out-of-range sequence item");
    CHECK(toy_pop(first, 1), "pop constructed vector");

    CHECK(toy_push_string(first, "name", 4) == TOY_OK &&
              toy_push_string(first, "Ada", 3) == TOY_OK &&
              toy_push_string(first, "scores", 6) == TOY_OK &&
              toy_push_value(first, scores) == TOY_OK,
          "push map pairs");
    CHECK(toy_make_map(first, 2) == TOY_OK, "make map from stack");
    toy_value *profile = toy_value_retain(first, 0);
    CHECK(profile && toy_value_type(profile) == TOY_TYPE_MAP,
          "retain constructed map");
    size_t map_size = 0;
    CHECK(toy_map_size(profile, &map_size) && map_size == 2,
          "constructed map size");

    toy_value *map_key = NULL;
    toy_value *map_value = NULL;
    CHECK(toy_map_entry(profile, 0, &map_key, &map_value),
          "read first map entry");
    CHECK(toy_value_get_string(map_key, &text, &length) && length == 4 &&
              memcmp(text, "name", 4) == 0,
          "first map key preserves insertion order");
    CHECK(toy_value_get_string(map_value, &text, &length) && length == 3 &&
              memcmp(text, "Ada", 3) == 0,
          "first map value");
    toy_value_release(map_key);
    toy_value_release(map_value);

    CHECK(toy_map_entry(profile, 1, &map_key, &map_value),
          "read second map entry");
    CHECK(toy_value_get_string(map_key, &text, &length) && length == 6 &&
              memcmp(text, "scores", 6) == 0 &&
              toy_sequence_size(map_value, &sequence_size) &&
              sequence_size == 3,
          "second map entry contains vector");
    toy_value_release(map_key);
    toy_value_release(map_value);
    CHECK(!toy_map_entry(profile, 2, &map_key, &map_value),
          "reject out-of-range map entry");

    CHECK(toy_pop(first, 1), "pop constructed map");
    CHECK(toy_push_value(first, profile) == TOY_OK,
          "restore retained map to stack");
    CHECK(toy_eval(first, "<retained-map>",
                   "\"scores\" get 0 swap [ + ] fold") == TOY_OK,
          "use retained map from Toy");
    CHECK(toy_get_int(first, 0, &integer) && integer == 12,
          "retained map result");
    CHECK(toy_pop(first, 1), "pop retained map result");

    CHECK(toy_push_value(second, profile) == TOY_ERROR,
          "reject a retained value in another state");
    CHECK(toy_stack_size(second) == 0,
          "cross-state value rejection preserves stack");

    CHECK(toy_eval(first, "<retained-callable>", "[ 3 * ]") == TOY_OK,
          "create callable value");
    toy_value *triple = toy_value_retain(first, 0);
    CHECK(triple && toy_value_type(triple) == TOY_TYPE_VECTOR,
          "retain callable value");
    CHECK(toy_pop(first, 1), "pop original callable");
    CHECK(toy_push_int(first, 14) == TOY_OK, "push callable argument");
    CHECK(toy_call_value(first, triple) == TOY_OK,
          "call retained quotation");
    CHECK(toy_get_int(first, 0, &integer) && integer == 42,
          "retained quotation result");
    CHECK(toy_pop(first, 1), "pop callable result");
    CHECK(toy_call_value(second, triple) == TOY_ERROR,
          "reject cross-state callable");

    score = toy_sequence_get(scores, 0);
    CHECK(score && toy_call_value(first, score) == TOY_ERROR,
          "reject retained non-callable");
    toy_value_release(score);

    CHECK(toy_make_vector(first, 1) == TOY_ERROR,
          "vector construction detects stack underflow");
    CHECK(toy_stack_size(first) == 0,
          "vector construction underflow preserves stack");
    CHECK(toy_make_vector(first, 0) == TOY_OK,
          "make empty vector for invalid map key");
    CHECK(toy_push_int(first, 1) == TOY_OK,
          "push invalid map value");
    CHECK(toy_make_map(first, 1) == TOY_ERROR,
          "map construction rejects unhashable key");
    CHECK(toy_stack_size(first) == 2,
          "invalid map construction preserves inputs");
    CHECK(toy_pop(first, 2), "clear invalid map inputs");

    CHECK(toy_eval(first, "<list-value>", "( 7 8 )") == TOY_OK,
          "create list value");
    toy_value *list_value = toy_value_retain(first, 0);
    CHECK(list_value && toy_sequence_size(list_value, &sequence_size) &&
              sequence_size == 2,
          "list sequence size");
    toy_value *list_item = toy_sequence_get(list_value, 1);
    CHECK(list_item && toy_value_get_int(list_item, &integer) && integer == 8,
          "list sequence item");
    toy_value_release(list_item);
    toy_value_release(list_value);
    CHECK(toy_pop(first, 1), "pop list value");

    toy_value_release(triple);
    toy_value_release(profile);
    toy_value_release(scores);

    size_t stack_before_resource = toy_stack_size(first);
    int rejected_resource = 0;
    CHECK(toy_push_resource(first, "", &rejected_resource,
                            destroy_test_resource, NULL) == TOY_ERROR,
          "reject empty resource type");
    CHECK(toy_push_resource(first, "bad type", &rejected_resource,
                            destroy_test_resource, NULL) == TOY_ERROR,
          "reject invalid resource type");
    CHECK(toy_push_resource(first, "host.widget", NULL,
                            destroy_test_resource, NULL) == TOY_ERROR,
          "reject null resource pointer");
    CHECK(toy_stack_size(first) == stack_before_resource,
          "rejected resources do not change the stack");

    int resource_destroy_count = 0;
    int *widget = malloc(sizeof(*widget));
    CHECK(widget, "allocate test resource");
    *widget = 42;
    char mutable_type[] = "host.widget";
    CHECK(toy_push_resource(first, mutable_type, widget,
                            destroy_test_resource,
                            &resource_destroy_count) == TOY_OK,
          "push resource");
    mutable_type[0] = 'X';
    CHECK(toy_stack_type(first, 0) == TOY_TYPE_RESOURCE,
          "resource stack type");

    void *borrowed_resource = NULL;
    const char *resource_type = NULL;
    CHECK(toy_get_resource(first, 0, "host.widget", &borrowed_resource) &&
              borrowed_resource == widget && *(int *)borrowed_resource == 42,
          "typed resource lookup");
    CHECK(!toy_get_resource(first, 0, "host.other", &borrowed_resource),
          "resource type mismatch");
    CHECK(toy_get_resource_type(first, 0, &resource_type) &&
              strcmp(resource_type, "host.widget") == 0,
          "resource type name is copied");

    toy_value *resource_value = toy_value_retain(first, 0);
    CHECK(resource_value &&
              toy_value_get_resource(resource_value, "host.widget",
                                     &borrowed_resource) &&
              borrowed_resource == widget &&
              toy_value_get_resource_type(resource_value, &resource_type) &&
              strcmp(resource_type, "host.widget") == 0,
          "retain and inspect resource");

    CHECK(toy_eval(first, "<resource-introspection>",
                   "dup resource? dup type-of") == TOY_OK,
          "resource introspection");
    CHECK(toy_get_string(first, 0, &text, &length) && length == 4 &&
              memcmp(text, "bool", 4) == 0,
          "resource predicate result type");
    CHECK(toy_pop(first, 1), "pop resource predicate type");
    bool resource_predicate = false;
    CHECK(toy_get_bool(first, 0, &resource_predicate) && resource_predicate,
          "resource predicate result");
    CHECK(toy_pop(first, 1), "pop resource predicate");

    CHECK(toy_eval(first, "<resource-repr>", "dup repr") == TOY_OK,
          "resource representation");
    CHECK(toy_get_string(first, 0, &text, &length) &&
              length == strlen("<resource:host.widget>") &&
              memcmp(text, "<resource:host.widget>", length) == 0,
          "resource representation includes its type");
    CHECK(toy_pop(first, 1), "pop resource representation");

    CHECK(toy_eval(first, "<resource-identity>", "dup dup ==") == TOY_OK,
          "resource identity equality");
    CHECK(toy_get_bool(first, 0, &resource_predicate) && resource_predicate,
          "same resource wrapper compares equal");
    CHECK(toy_pop(first, 1), "pop resource equality result");

    CHECK(toy_eval(first, "<resource-unhashable>",
                   "#{ } over insert") == TOY_ERROR,
          "resource cannot be inserted into a set");
    CHECK(toy_get_error(first) &&
              strstr(toy_get_error(first),
                     "expected hashable set item, found resource"),
          "resource unhashable diagnostic");
    CHECK(toy_pop(first, 2), "clear rejected set insertion inputs");
    CHECK(resource_destroy_count == 0,
          "rejected set insertion preserves original resource");

    CHECK(toy_eval(first, "<resource-collection>",
                   "[ ] swap push-back") == TOY_OK,
          "store resource in a vector");
    CHECK(resource_destroy_count == 0,
          "collection retains resource ownership");
    CHECK(toy_pop(first, 1), "release resource collection");
    CHECK(resource_destroy_count == 0,
          "retained resource outlives stack references");
    toy_value_release(resource_value);
    CHECK(resource_destroy_count == 1,
          "resource destructor runs once after retained release");

    CHECK(toy_eval(first, "<resource-error>", "host.fail-resource") ==
              TOY_ERROR,
          "native error after creating resource");
    CHECK(failed_resource_destroy_count == 0 &&
              toy_stack_type(first, 0) == TOY_TYPE_RESOURCE,
          "resource survives non-transactional error stack effects");
    CHECK(toy_pop(first, 1), "release failed-call resource");
    CHECK(failed_resource_destroy_count == 1,
          "failed-call resource is destroyed once");

    int shutdown_destroy_count = 0;
    int *shutdown_resource = malloc(sizeof(*shutdown_resource));
    CHECK(shutdown_resource, "allocate shutdown resource");
    *shutdown_resource = 7;
    CHECK(toy_push_resource(second, "host.shutdown", shutdown_resource,
                            destroy_test_resource,
                            &shutdown_destroy_count) == TOY_OK,
          "push state-owned shutdown resource");

    CHECK(toy_eval(first, "<live-list-slab>", "0 1000 range >list") == TOY_OK,
          "create list spanning several slabs");

    toy_state_free(second);
    CHECK(shutdown_destroy_count == 1,
          "state shutdown destroys its remaining resources");
    CHECK(toy_eval(first, "<list-after-cache-clear>",
                   "dup 999 contains?") == TOY_OK,
          "list remains valid after another state clears empty caches");
    bool list_survived_cache_clear = false;
    CHECK(toy_get_bool(first, 0, &list_survived_cache_clear) &&
              list_survived_cache_clear,
          "live list contents survive cache cleanup");
    CHECK(toy_pop(first, 2), "release live list and cache-cleanup result");
    toy_state_free(first);
#ifdef STB_LEAKCHECK
    stb_leakcheck_dumpmem();
#endif
    return 0;
}

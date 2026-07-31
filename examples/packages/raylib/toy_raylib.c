#define TOY_EXTENSION_IMPLEMENTATION
#include "toy.h"

#include <raylib.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RAYLIB_TEXTURE_TYPE "raylib.texture"

static bool get_int32(toy_state *state, size_t depth, int *value) {
    int64_t integer = 0;
    if (!toy_get_int(state, depth, &integer) || integer < INT_MIN ||
        integer > INT_MAX) {
        return false;
    }
    *value = (int)integer;
    return true;
}

static bool get_byte(toy_state *state, size_t depth, unsigned char *value) {
    int64_t integer = 0;
    if (!toy_get_int(state, depth, &integer) || integer < 0 || integer > 255) {
        return false;
    }
    *value = (unsigned char)integer;
    return true;
}

static bool get_number(toy_state *state, size_t depth, float *value) {
    double number = 0.0;
    int64_t integer = 0;
    if (toy_get_float(state, depth, &number)) {
        if (!isfinite(number) || number < -FLT_MAX || number > FLT_MAX) {
            return false;
        }
        *value = (float)number;
        return true;
    }
    if (toy_get_int(state, depth, &integer)) {
        *value = (float)integer;
        return true;
    }
    return false;
}

static bool get_color(toy_state *state, size_t depth, Color *color) {
    int64_t packed = 0;
    if (!toy_get_int(state, depth, &packed) || packed < 0 ||
        (uint64_t)packed > UINT32_MAX) {
        return false;
    }
    uint32_t rgba = (uint32_t)packed;
    color->r = (unsigned char)(rgba >> 24);
    color->g = (unsigned char)(rgba >> 16);
    color->b = (unsigned char)(rgba >> 8);
    color->a = (unsigned char)rgba;
    return true;
}

static char *copy_c_string(toy_state *state, size_t depth,
                           bool *contains_nul) {
    const char *data = NULL;
    size_t length = 0;
    *contains_nul = false;
    if (!toy_get_string(state, depth, &data, &length) || length == SIZE_MAX) {
        return NULL;
    }
    if (memchr(data, '\0', length)) {
        *contains_nul = true;
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, data, length);
    copy[length] = '\0';
    return copy;
}

static void destroy_texture(void *resource, void *userdata) {
    (void)userdata;
    Texture2D *texture = resource;
    UnloadTexture(*texture);
    free(texture);
}

static toy_status raylib_init_window(toy_state *state) {
    int width = 0;
    int height = 0;
    if (!get_int32(state, 2, &width) || !get_int32(state, 1, &height) ||
        width <= 0 || height <= 0 ||
        toy_stack_type(state, 0) != TOY_TYPE_STRING) {
        return toy_fail(
            state,
            "raylib.init-window expected positive width and height integers "
            "and a title string");
    }
    if (IsWindowReady()) {
        return toy_fail(state, "raylib.init-window window is already open");
    }

    bool contains_nul = false;
    char *title = copy_c_string(state, 0, &contains_nul);
    if (!title) {
        if (contains_nul) {
            return toy_fail(
                state,
                "raylib.init-window title contains an embedded NUL byte");
        }
        return toy_fail(state,
                             "raylib.init-window could not copy the title");
    }
    if (!toy_pop(state, 3)) {
        free(title);
        return toy_fail(state,
                             "raylib.init-window failed to pop its inputs");
    }

    InitWindow(width, height, title);
    free(title);
    if (!IsWindowReady()) {
        return toy_fail(
            state, "raylib.init-window failed to initialize the window");
    }
    return TOY_OK;
}

static toy_status raylib_close_window(toy_state *state) {
    (void)state;
    if (IsWindowReady()) CloseWindow();
    return TOY_OK;
}

static toy_status raylib_window_should_close(toy_state *state) {
    return toy_push_bool(state, WindowShouldClose());
}

static toy_status raylib_set_target_fps(toy_state *state) {
    int fps = 0;
    if (!get_int32(state, 0, &fps) || fps <= 0) {
        return toy_fail(
            state,
            "raylib.set-target-fps expected a positive FPS integer");
    }
    if (!toy_pop(state, 1)) {
        return toy_fail(
            state, "raylib.set-target-fps failed to pop its input");
    }
    SetTargetFPS(fps);
    return TOY_OK;
}

static toy_status raylib_begin_drawing(toy_state *state) {
    (void)state;
    BeginDrawing();
    return TOY_OK;
}

static toy_status raylib_end_drawing(toy_state *state) {
    (void)state;
    EndDrawing();
    return TOY_OK;
}

static toy_status raylib_rgba(toy_state *state) {
    unsigned char red = 0;
    unsigned char green = 0;
    unsigned char blue = 0;
    unsigned char alpha = 0;
    if (!get_byte(state, 3, &red) || !get_byte(state, 2, &green) ||
        !get_byte(state, 1, &blue) || !get_byte(state, 0, &alpha)) {
        return toy_fail(
            state,
            "raylib.rgba expected four integers between 0 and 255");
    }
    uint32_t packed = ((uint32_t)red << 24) | ((uint32_t)green << 16) |
                      ((uint32_t)blue << 8) | alpha;
    if (!toy_pop(state, 4)) {
        return toy_fail(state, "raylib.rgba failed to pop its inputs");
    }
    return toy_push_int(state, (int64_t)packed);
}

static toy_status raylib_clear_background(toy_state *state) {
    Color color = {0};
    if (!get_color(state, 0, &color)) {
        return toy_fail(
            state, "raylib.clear-background expected an RGBA color");
    }
    if (!toy_pop(state, 1)) {
        return toy_fail(
            state, "raylib.clear-background failed to pop its input");
    }
    ClearBackground(color);
    return TOY_OK;
}

static toy_status raylib_draw_circle(toy_state *state) {
    int center_x = 0;
    int center_y = 0;
    float radius = 0.0f;
    Color color = {0};
    if (!get_int32(state, 3, &center_x) ||
        !get_int32(state, 2, &center_y) ||
        !get_number(state, 1, &radius) || radius < 0.0f ||
        !get_color(state, 0, &color)) {
        return toy_fail(
            state,
            "raylib.draw-circle expected x y radius and an RGBA color");
    }
    if (!toy_pop(state, 4)) {
        return toy_fail(state,
                             "raylib.draw-circle failed to pop its inputs");
    }
    DrawCircle(center_x, center_y, radius, color);
    return TOY_OK;
}

static toy_status raylib_draw_rectangle(toy_state *state) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    Color color = {0};
    if (!get_int32(state, 4, &x) || !get_int32(state, 3, &y) ||
        !get_int32(state, 2, &width) ||
        !get_int32(state, 1, &height) || width < 0 || height < 0 ||
        !get_color(state, 0, &color)) {
        return toy_fail(
            state,
            "raylib.draw-rectangle expected x y non-negative width and "
            "height and an RGBA color");
    }
    if (!toy_pop(state, 5)) {
        return toy_fail(
            state, "raylib.draw-rectangle failed to pop its inputs");
    }
    DrawRectangle(x, y, width, height, color);
    return TOY_OK;
}

static toy_status raylib_draw_text(toy_state *state) {
    int x = 0;
    int y = 0;
    int font_size = 0;
    Color color = {0};
    if (toy_stack_type(state, 4) != TOY_TYPE_STRING ||
        !get_int32(state, 3, &x) || !get_int32(state, 2, &y) ||
        !get_int32(state, 1, &font_size) || font_size <= 0 ||
        !get_color(state, 0, &color)) {
        return toy_fail(
            state,
            "raylib.draw-text expected text x y positive-font-size and an "
            "RGBA color");
    }

    bool contains_nul = false;
    char *text = copy_c_string(state, 4, &contains_nul);
    if (!text) {
        if (contains_nul) {
            return toy_fail(
                state, "raylib.draw-text text contains an embedded NUL byte");
        }
        return toy_fail(state,
                             "raylib.draw-text could not copy the text");
    }
    if (!toy_pop(state, 5)) {
        free(text);
        return toy_fail(state,
                             "raylib.draw-text failed to pop its inputs");
    }
    DrawText(text, x, y, font_size, color);
    free(text);
    return TOY_OK;
}

static toy_status raylib_load_texture(toy_state *state) {
    if (toy_stack_type(state, 0) != TOY_TYPE_STRING) {
        return toy_fail(state,
                             "raylib.load-texture expected a path string");
    }
    if (!IsWindowReady()) {
        return toy_fail(state,
                             "raylib.load-texture requires an open window");
    }

    bool contains_nul = false;
    char *path = copy_c_string(state, 0, &contains_nul);
    if (!path) {
        if (contains_nul) {
            return toy_fail(
                state,
                "raylib.load-texture path contains an embedded NUL byte");
        }
        return toy_fail(state,
                             "raylib.load-texture could not copy the path");
    }
    if (!toy_pop(state, 1)) {
        free(path);
        return toy_fail(state,
                             "raylib.load-texture failed to pop its input");
    }

    Texture2D loaded = LoadTexture(path);
    free(path);
    if (loaded.id == 0) {
        return toy_fail(
            state, "raylib.load-texture failed to load the texture");
    }

    Texture2D *texture = malloc(sizeof(*texture));
    if (!texture) {
        UnloadTexture(loaded);
        return toy_fail(
            state, "raylib.load-texture could not allocate its handle");
    }
    *texture = loaded;
    toy_status status = toy_push_resource(state, RAYLIB_TEXTURE_TYPE, texture,
                                          destroy_texture, NULL);
    if (status != TOY_OK) {
        destroy_texture(texture, NULL);
        return status;
    }
    return TOY_OK;
}

static toy_status raylib_draw_texture(toy_state *state) {
    Texture2D *texture = NULL;
    int x = 0;
    int y = 0;
    Color tint = {0};
    void *resource = NULL;
    if (!toy_get_resource(state, 3, RAYLIB_TEXTURE_TYPE, &resource) ||
        !get_int32(state, 2, &x) || !get_int32(state, 1, &y) ||
        !get_color(state, 0, &tint)) {
        return toy_fail(
            state,
            "raylib.draw-texture expected a raylib texture, x, y, and an "
            "RGBA tint");
    }
    texture = resource;
    DrawTexture(*texture, x, y, tint);
    if (!toy_pop(state, 4)) {
        return toy_fail(
            state, "raylib.draw-texture failed to pop its inputs");
    }
    return TOY_OK;
}

static toy_status raylib_mouse_x(toy_state *state) {
    return toy_push_int(state, GetMouseX());
}

static toy_status raylib_mouse_y(toy_state *state) {
    return toy_push_int(state, GetMouseY());
}

static toy_status raylib_frame_time(toy_state *state) {
    return toy_push_float(state, GetFrameTime());
}

static const toy_native_word raylib_words[] = {
    {.name = "init-window", .callback = raylib_init_window},
    {.name = "close-window", .callback = raylib_close_window},
    {.name = "window-should-close?", .callback = raylib_window_should_close},
    {.name = "set-target-fps", .callback = raylib_set_target_fps},
    {.name = "begin-drawing", .callback = raylib_begin_drawing},
    {.name = "end-drawing", .callback = raylib_end_drawing},
    {.name = "rgba", .callback = raylib_rgba},
    {.name = "clear-background", .callback = raylib_clear_background},
    {.name = "draw-circle", .callback = raylib_draw_circle},
    {.name = "draw-rectangle", .callback = raylib_draw_rectangle},
    {.name = "draw-text", .callback = raylib_draw_text},
    {.name = "load-texture", .callback = raylib_load_texture},
    {.name = "draw-texture", .callback = raylib_draw_texture},
    {.name = "mouse-x", .callback = raylib_mouse_x},
    {.name = "mouse-y", .callback = raylib_mouse_y},
    {.name = "frame-time", .callback = raylib_frame_time},
};

static const toy_extension raylib_extension = {
    sizeof(toy_extension),
    "raylib",
    raylib_words,
    sizeof(raylib_words) / sizeof(raylib_words[0]),
};

TOY_EXTENSION_EXPORT const toy_extension *toy_extension_init(
    uint32_t abi_version, const toy_extension_api *api) {
    if (!toy_extension_bind(abi_version, api)) return NULL;
    return &raylib_extension;
}

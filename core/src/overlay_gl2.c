// In-game overlay: Nuklear rendered directly into Minecraft's own GL frame,
// instead of the external Swift app + IPC socket v1 shipped with.
//
// This is a fork of Nuklear's demo/glfw_opengl2/nuklear_glfw_gl2.h, not a
// straight vendor of it - that file bare-calls real GLFW functions
// (glfwGetWindowSize, glfwSetInputMode, glfwGetClipboardString, the
// glfwSet*Callback registration itself, ...) which would fail to link here:
// this project never links the real GLFW dylib at all (LWJGL bundles its
// own copy at a runtime-extracted temp path, which is exactly why
// interpose.c uses dlsym interposition instead of static linking). Only the
// pure GL/Nuklear rendering and font-atlas-upload code is reusable
// verbatim; window/input queries go through resolve_cached() (mirroring
// window_control.c) or input_shim.c's captured state instead.
#include "overlay_gl2.h"

// macOS deprecated the legacy fixed-function GL API in 10.14, but it's
// still fully functional and is what this project's GL context is stuck
// on anyway (a GL 2.1 compatibility profile, per tuxinjector's own findings
// - see docs/RISKS.md) - silence the warnings rather than working around
// deprecated-but-working, still-supported entry points.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <stddef.h>
#include <stdio.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "../third_party/nuklear.h"

#include "hotkeys.h"
#include "input_shim.h"
#include "interpose.h"
#include "key_rebind.h"
#include "log.h"
#include "modes.h"
#include "window_control.h"

struct overlay_vertex {
    float position[2];
    float uv[2];
    nk_byte col[4];
};

typedef void (*get_window_size_fn)(GLFWwindow *, int *, int *);
typedef void (*get_framebuffer_size_fn)(GLFWwindow *, int *, int *);

static struct nk_context g_ctx;
static struct nk_font_atlas g_atlas;
static struct nk_buffer g_cmds;
static struct nk_draw_null_texture g_tex_null;
static GLuint g_font_tex;
static bool g_initialized = false;

static get_window_size_fn g_get_window_size = NULL;
static get_framebuffer_size_fn g_get_framebuffer_size = NULL;

static void overlay_init(void) {
    nk_init_default(&g_ctx, 0);
    nk_buffer_init_default(&g_cmds);

    nk_font_atlas_init_default(&g_atlas);
    nk_font_atlas_begin(&g_atlas);
    // nk_font_atlas_add_default() only returns the font - unlike the
    // "no font added yet" fallback path inside nk_font_atlas_bake() itself
    // (nuklear.h ~18038), it does NOT also assign atlas->default_font, so
    // callers who add their own default font explicitly (as here) must
    // capture the return value themselves. Skipping this left
    // g_atlas.default_font NULL, and &NULL->handle computed a
    // non-NULL-but-garbage pointer that got installed as the style's font
    // - crashed the first time any widget (nk_begin's title text) tried to
    // measure text through it.
    g_atlas.default_font = nk_font_atlas_add_default(&g_atlas, 13.0f, NULL);

    int w, h;
    const void *image = nk_font_atlas_bake(&g_atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    glGenTextures(1, &g_font_tex);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    nk_font_atlas_end(&g_atlas, nk_handle_id((int)g_font_tex), &g_tex_null);
    if (g_atlas.default_font) {
        nk_style_set_font(&g_ctx, &g_atlas.default_font->handle);
    }

    applescreen_log("overlay: initialized (font atlas %dx%d, tex=%u)", w, h, g_font_tex);
}

typedef enum {
    OVERLAY_TAB_MODES,
    OVERLAY_TAB_HOTKEYS,
    OVERLAY_TAB_REBINDS,
    OVERLAY_TAB_SENSITIVITY,
    OVERLAY_TAB_COUNT,
} overlay_tab_t;

static overlay_tab_t g_active_tab = OVERLAY_TAB_MODES;

static void overlay_tab_bar(void) {
    static const char *labels[OVERLAY_TAB_COUNT] = {"Modes", "Hotkeys", "Rebinds", "Sensitivity"};
    nk_layout_row_dynamic(&g_ctx, 22, OVERLAY_TAB_COUNT);
    for (int i = 0; i < OVERLAY_TAB_COUNT; i++) {
        struct nk_style_button style = g_ctx.style.button;
        if ((overlay_tab_t)i == g_active_tab) {
            style.normal = nk_style_item_color(nk_rgb(80, 120, 200));
            style.hover = style.normal;
        }
        if (nk_button_label_styled(&g_ctx, &style, labels[i])) {
            g_active_tab = (overlay_tab_t)i;
            applescreen_input_shim_cancel_capture();
        }
    }
}

static void overlay_modes_tab(void) {
    int x = 0, y = 0, w = 0, h = 0;
    char buf[64];
    const char *active_id = applescreen_modes_active_id();

    applescreen_window_control_get_pos(&x, &y);
    applescreen_window_control_get_size(&w, &h);

    nk_layout_row_dynamic(&g_ctx, 20, 1);
    snprintf(buf, sizeof(buf), "Position: %d, %d", x, y);
    nk_label(&g_ctx, buf, NK_TEXT_LEFT);
    snprintf(buf, sizeof(buf), "Size: %d x %d", w, h);
    nk_label(&g_ctx, buf, NK_TEXT_LEFT);
    snprintf(buf, sizeof(buf), "Mode: %s", active_id ? active_id : "(none)");
    nk_label(&g_ctx, buf, NK_TEXT_LEFT);

    nk_layout_row_dynamic(&g_ctx, 4, 1);
    nk_spacing(&g_ctx, 1);

    nk_layout_row_dynamic(&g_ctx, 25, 1);
    int mode_count = applescreen_modes_count();
    for (int i = 0; i < mode_count; i++) {
        const applescreen_mode_t *mode = applescreen_modes_get(i);
        if (nk_button_label(&g_ctx, mode->id)) {
            applescreen_modes_switch(mode->id);
        }
    }

    nk_layout_row_dynamic(&g_ctx, 4, 1);
    nk_spacing(&g_ctx, 1);

    nk_layout_row_dynamic(&g_ctx, 25, 1);
    if (nk_button_label(&g_ctx, "Focus Window")) {
        applescreen_window_control_focus();
    }
}

static void overlay_hotkeys_tab(void) {
    char buf[64];
    bool capturing = applescreen_input_shim_capturing();

    if (capturing) {
        nk_layout_row_dynamic(&g_ctx, 20, 1);
        nk_label_colored(&g_ctx, "Press a key... (Ctrl+I cancels, times out if idle)", NK_TEXT_LEFT,
                          nk_rgb(230, 200, 80));
        const char *error = applescreen_input_shim_capture_error();
        if (error) {
            nk_label_colored(&g_ctx, error, NK_TEXT_LEFT, nk_rgb(220, 90, 90));
        }
        nk_layout_row_dynamic(&g_ctx, 4, 1);
        nk_spacing(&g_ctx, 1);
    }

    int count = applescreen_hotkeys_count();
    for (int i = 0; i < count; i++) {
        int key = 0, mods = 0;
        bool bound = applescreen_hotkeys_get_binding(i, &key, &mods);
        const char *mode_id = applescreen_hotkeys_mode_id(i);

        char key_label[48] = "(unbound)";
        if (bound) applescreen_input_shim_format_key_label(key, mods, key_label, sizeof(key_label));

        nk_layout_row_begin(&g_ctx, NK_STATIC, 25, 2);
        nk_layout_row_push(&g_ctx, 150);
        snprintf(buf, sizeof(buf), "%s: %s", mode_id, key_label);
        nk_label(&g_ctx, buf, NK_TEXT_LEFT);
        nk_layout_row_push(&g_ctx, 90);
        if (nk_button_label(&g_ctx, "Rebind")) {
            applescreen_hotkeys_begin_rebind(i);
        }
        nk_layout_row_end(&g_ctx);
    }
}

static void overlay_rebinds_tab(void) {
    char buf[64];
    bool capturing = applescreen_input_shim_capturing();

    if (capturing) {
        nk_layout_row_dynamic(&g_ctx, 20, 1);
        nk_label_colored(&g_ctx, "Press a key... (Ctrl+I cancels, times out if idle)", NK_TEXT_LEFT,
                          nk_rgb(230, 200, 80));
        const char *error = applescreen_input_shim_capture_error();
        if (error) {
            nk_label_colored(&g_ctx, error, NK_TEXT_LEFT, nk_rgb(220, 90, 90));
        }
        nk_layout_row_dynamic(&g_ctx, 4, 1);
        nk_spacing(&g_ctx, 1);
    }

    int count = applescreen_key_rebind_count();
    for (int i = 0; i < count; i++) {
        int from_key = 0, to_key = 0, mods = 0;
        if (!applescreen_key_rebind_get(i, &from_key, &to_key, &mods)) continue;

        char from_label[48], to_label[48];
        applescreen_input_shim_format_key_label(from_key, mods, from_label, sizeof(from_label));
        applescreen_input_shim_format_key_label(to_key, 0, to_label, sizeof(to_label));

        nk_layout_row_begin(&g_ctx, NK_STATIC, 25, 2);
        nk_layout_row_push(&g_ctx, 190);
        snprintf(buf, sizeof(buf), "%s -> %s", from_label, to_label);
        nk_label(&g_ctx, buf, NK_TEXT_LEFT);
        nk_layout_row_push(&g_ctx, 60);
        if (nk_button_label(&g_ctx, "Remove")) {
            applescreen_key_rebind_remove(i);
        }
        nk_layout_row_end(&g_ctx);
    }

    nk_layout_row_dynamic(&g_ctx, 4, 1);
    nk_spacing(&g_ctx, 1);

    nk_layout_row_dynamic(&g_ctx, 25, 1);
    if (nk_button_label(&g_ctx, "Add Rebind")) {
        applescreen_key_rebind_begin_add();
    }
}

static void overlay_placeholder_tab(const char *name) {
    nk_layout_row_dynamic(&g_ctx, 20, 1);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s - coming soon", name);
    nk_label(&g_ctx, buf, NK_TEXT_LEFT);
}

static void overlay_build_ui(void) {
    if (nk_begin(&g_ctx, "Applescreen", nk_rect(50, 50, 320, 380),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE)) {
        overlay_tab_bar();
        nk_layout_row_dynamic(&g_ctx, 4, 1);
        nk_spacing(&g_ctx, 1);

        switch (g_active_tab) {
            case OVERLAY_TAB_MODES:
                overlay_modes_tab();
                break;
            case OVERLAY_TAB_HOTKEYS:
                overlay_hotkeys_tab();
                break;
            case OVERLAY_TAB_REBINDS:
                overlay_rebinds_tab();
                break;
            case OVERLAY_TAB_SENSITIVITY:
                overlay_placeholder_tab("Sensitivity");
                break;
            default:
                break;
        }
    }
    nk_end(&g_ctx);
}

// Forked from nk_glfw3_render - the GL calls themselves are copied
// near-verbatim (pure GL/Nuklear, no bare GLFW calls to begin with); only
// the source of width/height/fb_scale changed, from a persistent struct
// populated by bare glfwGet* calls to parameters resolved per-frame in
// applescreen_overlay_render_frame below.
static void overlay_render_gl(int window_width, int window_height, int fb_width, int fb_height) {
    struct nk_vec2 fb_scale;
    fb_scale.x = window_width > 0 ? (float)fb_width / (float)window_width : 1.0f;
    fb_scale.y = window_height > 0 ? (float)fb_height / (float)window_height : 1.0f;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0f, (float)window_width, (float)window_height, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    {
        GLsizei vs = sizeof(struct overlay_vertex);
        size_t vp = offsetof(struct overlay_vertex, position);
        size_t vt = offsetof(struct overlay_vertex, uv);
        size_t vc = offsetof(struct overlay_vertex, col);

        const struct nk_draw_command *cmd;
        const nk_draw_index *offset = NULL;
        struct nk_buffer vbuf, ebuf;
        struct nk_convert_config config;
        static const struct nk_draw_vertex_layout_element vertex_layout[] = {
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct overlay_vertex, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct overlay_vertex, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct overlay_vertex, col)},
            {NK_VERTEX_LAYOUT_END},
        };

        nk_zero(&config, sizeof(config));
        config.vertex_layout = vertex_layout;
        config.vertex_size = sizeof(struct overlay_vertex);
        config.vertex_alignment = NK_ALIGNOF(struct overlay_vertex);
        config.tex_null = g_tex_null;
        config.circle_segment_count = 22;
        config.curve_segment_count = 22;
        config.arc_segment_count = 22;
        config.global_alpha = 1.0f;
        config.shape_AA = NK_ANTI_ALIASING_ON;
        config.line_AA = NK_ANTI_ALIASING_ON;

        nk_buffer_init_default(&vbuf);
        nk_buffer_init_default(&ebuf);
        nk_convert(&g_ctx, &g_cmds, &vbuf, &ebuf, &config);

        {
            const void *vertices = nk_buffer_memory_const(&vbuf);
            glVertexPointer(2, GL_FLOAT, vs, (const void *)((const nk_byte *)vertices + vp));
            glTexCoordPointer(2, GL_FLOAT, vs, (const void *)((const nk_byte *)vertices + vt));
            glColorPointer(4, GL_UNSIGNED_BYTE, vs, (const void *)((const nk_byte *)vertices + vc));
        }

        offset = (const nk_draw_index *)nk_buffer_memory_const(&ebuf);
        nk_draw_foreach(cmd, &g_ctx, &g_cmds) {
            if (!cmd->elem_count) continue;
            glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
            glScissor((GLint)(cmd->clip_rect.x * fb_scale.x),
                      (GLint)((window_height - (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) * fb_scale.y),
                      (GLint)(cmd->clip_rect.w * fb_scale.x), (GLint)(cmd->clip_rect.h * fb_scale.y));
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count, GL_UNSIGNED_SHORT, offset);
            offset += cmd->elem_count;
        }
        nk_clear(&g_ctx);
        nk_buffer_clear(&g_cmds);
        nk_buffer_free(&vbuf);
        nk_buffer_free(&ebuf);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

void applescreen_overlay_render_frame(GLFWwindow *window) {
    if (!g_initialized) {
        overlay_init();
        g_initialized = true;
    }

    if (!g_get_window_size) {
        g_get_window_size = (get_window_size_fn)applescreen_resolve_glfw_symbol("glfwGetWindowSize");
    }
    if (!g_get_framebuffer_size) {
        g_get_framebuffer_size = (get_framebuffer_size_fn)applescreen_resolve_glfw_symbol("glfwGetFramebufferSize");
    }

    int window_width = 1, window_height = 1, fb_width = 1, fb_height = 1;
    if (g_get_window_size) g_get_window_size(window, &window_width, &window_height);
    if (g_get_framebuffer_size) g_get_framebuffer_size(window, &fb_width, &fb_height);

    applescreen_input_state_t input;
    applescreen_input_shim_consume_frame(&input);
    applescreen_input_shim_tick_capture_timeout();

    nk_input_begin(&g_ctx);
    nk_input_motion(&g_ctx, (int)input.cursor_x, (int)input.cursor_y);
    nk_input_button(&g_ctx, NK_BUTTON_LEFT, (int)input.cursor_x, (int)input.cursor_y, input.mouse_down[0]);
    nk_input_button(&g_ctx, NK_BUTTON_RIGHT, (int)input.cursor_x, (int)input.cursor_y, input.mouse_down[1]);
    nk_input_button(&g_ctx, NK_BUTTON_MIDDLE, (int)input.cursor_x, (int)input.cursor_y, input.mouse_down[2]);
    nk_input_scroll(&g_ctx, nk_vec2((float)input.scroll_x, (float)input.scroll_y));
    nk_input_end(&g_ctx);

    if (!applescreen_input_shim_overlay_open()) {
        return;
    }

    overlay_build_ui();
    overlay_render_gl(window_width, window_height, fb_width, fb_height);
}

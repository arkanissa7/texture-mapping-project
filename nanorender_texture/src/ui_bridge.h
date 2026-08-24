#pragma once
#include "MiniFB.h"
extern "C" {
#include "microui.h"
}

static char g_pending_text[32] = {};
static int g_pending_text_len = 0;

inline void ui_bridge_char_input(struct mfb_window* window, unsigned int codepoint) {
    (void)window;
    if (codepoint < 0x80 && g_pending_text_len < (int)sizeof(g_pending_text) - 1) {
        g_pending_text[g_pending_text_len++] = (char)codepoint;
        g_pending_text[g_pending_text_len] = '\0';
    }
}

inline void ui_bridge_input(mu_Context* ctx, struct mfb_window* window) {
    if (g_pending_text_len > 0) {
        mu_input_text(ctx, g_pending_text);
        g_pending_text_len = 0;
        g_pending_text[0] = '\0';
    }

    // Mouse Position
    mu_input_mousemove(ctx, mfb_get_mouse_x(window), mfb_get_mouse_y(window));

    // Mouse Buttons — only fire down/up on state transitions
    static uint8_t prev_mouse[8] = {};
    const uint8_t* mouse_btn = mfb_get_mouse_button_buffer(window);
    int mx = mfb_get_mouse_x(window), my = mfb_get_mouse_y(window);

    auto sync_mouse = [&](int mfb_btn, int mu_btn) {
        uint8_t cur = mouse_btn[mfb_btn];
        if (cur && !prev_mouse[mfb_btn])  mu_input_mousedown(ctx, mx, my, mu_btn);
        if (!cur && prev_mouse[mfb_btn])  mu_input_mouseup  (ctx, mx, my, mu_btn);
        prev_mouse[mfb_btn] = cur;
    };
    sync_mouse(MFB_MOUSE_LEFT,   MU_MOUSE_LEFT);
    sync_mouse(MFB_MOUSE_RIGHT,  MU_MOUSE_RIGHT);
    sync_mouse(MFB_MOUSE_MIDDLE, MU_MOUSE_MIDDLE);

    // Mouse Scroll
    float scroll_y = mfb_get_mouse_scroll_y(window);
    if (scroll_y != 0) {
        mu_input_scroll(ctx, 0, (int)(scroll_y * -10));
    }

    // Keyboard — only fire down/up on state transitions
    static uint8_t prev_keys[MFB_KB_KEY_LAST + 1] = {};
    const uint8_t* keys = mfb_get_key_buffer(window);

    auto sync_key = [&](int mfb_key, int mu_key) {
        uint8_t cur = keys[mfb_key];
        if (cur && !prev_keys[mfb_key])  mu_input_keydown(ctx, mu_key);
        if (!cur && prev_keys[mfb_key])  mu_input_keyup  (ctx, mu_key);
        prev_keys[mfb_key] = cur;
    };
    sync_key(MFB_KB_KEY_LEFT_SHIFT,    MU_KEY_SHIFT);
    sync_key(MFB_KB_KEY_RIGHT_SHIFT,   MU_KEY_SHIFT);
    sync_key(MFB_KB_KEY_LEFT_CONTROL,  MU_KEY_CTRL);
    sync_key(MFB_KB_KEY_RIGHT_CONTROL, MU_KEY_CTRL);
    sync_key(MFB_KB_KEY_LEFT_ALT,      MU_KEY_ALT);
    sync_key(MFB_KB_KEY_RIGHT_ALT,     MU_KEY_ALT);
    sync_key(MFB_KB_KEY_ENTER,         MU_KEY_RETURN);
    sync_key(MFB_KB_KEY_KP_ENTER,      MU_KEY_RETURN);
    sync_key(MFB_KB_KEY_BACKSPACE,     MU_KEY_BACKSPACE);
}

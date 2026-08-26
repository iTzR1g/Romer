#include "ui.h"
#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace fs = std::filesystem;

// ─── Font ─────────────────────────────────────────────────────
static TTF_Font* g_font      = nullptr;
static TTF_Font* g_font_big  = nullptr;
static TTF_Font* g_font_mono = nullptr;

static bool try_open_font(const char* path, int size, TTF_Font*& font) {
    if (!font) {
        font = TTF_OpenFont(path, size);
        return font != nullptr;
    }
    return true;
}

static bool try_font_path(const char* path) {
    if (!path) return false;
    g_font = TTF_OpenFont(path, 16);
    if (!g_font) return false;
    g_font_big = TTF_OpenFont(path, 24);
    return true;
}

static bool load_mono_font(const char* override_path) {
    static const char* mono_paths[] = {
        // Debian / Ubuntu
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        // Arch / Fedora / openSUSE
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/LiberationMono-Regular.ttf",
        // Fedora / Gentoo / Void / Clear Linux
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        // NixOS
        "/run/current-system/sw/share/X11/fonts/DejaVuSansMono.ttf",
        // macOS
        "/Library/Fonts/Courier New.ttf",
        // Windows
        "C:/Windows/Fonts/consola.ttf",
    };
    if (override_path) {
        g_font_mono = TTF_OpenFont(override_path, 14);
        if (g_font_mono) return true;
    }
    for (auto path : mono_paths) {
        g_font_mono = TTF_OpenFont(path, 14);
        if (g_font_mono) return true;
    }
    return false;
}

static bool search_font_dirs(const std::vector<std::string>& dirs) {
    const std::vector<std::string> preferred = {
        "DejaVuSans.ttf",
        "LiberationSans-Regular.ttf",
        "NotoSans-Regular.ttf",
        "Arial.ttf",
        "SegoeUI.ttf",
        "FreeSans.ttf",
    };

    for (auto& base : dirs) {
        try {
            for (auto& entry : fs::recursive_directory_iterator(base)) {
                if (!entry.is_regular_file()) continue;
                auto name = entry.path().filename().string();
                if (name.size() < 4) continue;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                bool is_font = false;
                if (lower.size() >= 4) {
                    auto ext = lower.substr(lower.size() - 4);
                    is_font = (ext == ".ttf" || ext == ".otf" || ext == ".ttc");
                }
                if (is_font) {
                    for (auto& pref : preferred) {
                        std::string lowpref = pref;
                        std::transform(lowpref.begin(), lowpref.end(), lowpref.begin(), ::tolower);
                        if (lower == lowpref) {
                            if (try_font_path(entry.path().string().c_str()))
                                return true;
                        }
                    }
                }
            }
        } catch (...) {
        }
    }

    for (auto& base : dirs) {
        try {
            for (auto& entry : fs::recursive_directory_iterator(base)) {
                if (!entry.is_regular_file()) continue;
                auto name = entry.path().filename().string();
                if (name.size() < 4) continue;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                bool is_font = false;
                if (lower.size() >= 4) {
                    auto ext = lower.substr(lower.size() - 4);
                    is_font = (ext == ".ttf" || ext == ".otf" || ext == ".ttc");
                }
                if (is_font) {
                    if (try_font_path(entry.path().string().c_str()))
                        return true;
                }
            }
        } catch (...) {
        }
    }
    return false;
}

bool ui_load_font(const char* override_path) {
    static const char* paths[] = {
        // Debian / Ubuntu
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        // Arch / Fedora / openSUSE
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
        // Fedora / Gentoo / Void / Clear Linux
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        // NixOS
        "/run/current-system/sw/share/X11/fonts/DejaVuSans.ttf",
        // macOS
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNS.ttf",
        "/Library/Fonts/Arial.ttf",
        // Windows
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };

    if (override_path) {
        if (try_font_path(override_path)) {
            load_mono_font(override_path);
            return true;
        }
        return false;
    }

    for (auto path : paths) {
        if (try_font_path(path)) {
            load_mono_font(nullptr);
            return true;
        }
    }

    std::vector<std::string> font_dirs = {
        "/usr/share/fonts/truetype",       // Debian / Ubuntu
        "/usr/share/fonts/TTF",            // Arch / Fedora
        "/usr/share/fonts/otf",            // Arch
        "/usr/share/fonts/dejavu",         // Fedora / Gentoo / Void
        "/usr/share/fonts",                // fallback for all distros
        "/run/current-system/sw/share/X11/fonts",  // NixOS
    };
    const char* home = std::getenv("HOME");
    if (home) font_dirs.push_back(std::string(home) + "/.local/share/fonts");

    if (search_font_dirs(font_dirs)) {
        load_mono_font(nullptr);
        return true;
    }

    return false;
}

void ui_shutdown() {
    if (g_font_mono) TTF_CloseFont(g_font_mono);
    if (g_font_big)  TTF_CloseFont(g_font_big);
    if (g_font)      TTF_CloseFont(g_font);
    g_font = g_font_big = g_font_mono = nullptr;
}

bool ui_init(SDL_Window*, SDL_Renderer*) {
    if (TTF_Init() < 0) {
        fprintf(stderr, "[ui] TTF_Init: %s\n", TTF_GetError());
        return false;
    }
    if (!ui_load_font()) {
        fprintf(stderr, "[ui] No font found – install a TTF font (e.g. dejavu, liberation, noto)\n");
        fprintf(stderr, "[ui] Tried: /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf\n");
        // We continue without a font – buttons still work (no text)
    }
    return true;
}

// ─── Drawing helpers ─────────────────────────────────────────
static SDL_Color c_bg    = { 24,  24,  30,  255 };
static SDL_Color c_panel = { 36,  36,  44,  255 };
static SDL_Color c_btn   = { 56,  90, 150,  255 };
static SDL_Color c_btn_h = { 70, 110, 190,  255 };
static SDL_Color c_red   = { 180, 50,  50,  255 };
static SDL_Color c_red_h = { 210, 60,  60,  255 };
static SDL_Color c_green = { 50,  170, 70,  255 };
static SDL_Color c_text  = { 210, 210, 215, 255 };
static SDL_Color c_dim   = { 130, 130, 140, 255 };
static SDL_Color c_field = { 48,  48,  58,  255 };

static void drawRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}
static void drawBorder(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

// Render a text string at (x,y). Returns the rendered width.
static int drawText(SDL_Renderer* r, int x, int y, const char* text,
                    SDL_Color col, TTF_Font* f = nullptr) {
    if (!text || !*text) return 0;
    if (!f) f = g_font;
    if (!f) return 0;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, text, col);
    if (!s) return 0;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    if (!t) { SDL_FreeSurface(s); return 0; }
    SDL_Rect dst = { x, y, s->w, s->h };
    SDL_RenderCopy(r, t, nullptr, &dst);
    int w = s->w;
    SDL_FreeSurface(s);
    SDL_DestroyTexture(t);
    return w;
}

static int textWidth(const char* text, TTF_Font* f = nullptr) {
    if (!f) f = g_font;
    if (!f || !text) return 0;
    int w = 0;
    TTF_SizeUTF8(f, text, &w, nullptr);
    return w;
}

static int fontHeight(TTF_Font* f = nullptr) {
    if (!f) f = g_font;
    return f ? TTF_FontHeight(f) : 16;
}

// ─── Button ──────────────────────────────────────────────────
// Returns true if the button was clicked this frame.
struct Button {
    int x, y, w, h;
    const char* text;
    SDL_Color bg;
    SDL_Color bg_hover;
    bool enabled = true;
};

static bool button(SDL_Renderer* r, const Button& b, bool hovered) {
    SDL_Color c = (hovered && b.enabled) ? b.bg_hover : b.bg;
    drawRect(r, b.x, b.y, b.w, b.h, c);
    if (!b.enabled) {
        SDL_Color gray = { 80, 80, 85, 255 };
        drawRect(r, b.x, b.y, b.w, b.h, gray);
    }
    drawBorder(r, b.x, b.y, b.w, b.h, { 80, 80, 95, 255 });
    int tw = textWidth(b.text);
    int th = fontHeight();
    drawText(r, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2, b.text, c_text, g_font_big);
    return hovered && b.enabled;
}

// ─── Text field ──────────────────────────────────────────────
static void drawField(SDL_Renderer* r, int x, int y, int w, int h,
                      const TextField& fld, bool show_cursor = false) {
    drawRect(r, x, y, w, h, c_field);
    drawBorder(r, x, y, w, h, fld.active ? c_btn : c_dim);
    int th = fontHeight();
    drawText(r, x + 6, y + (h - th) / 2, fld.buffer.c_str(), c_text);
    if (fld.active && show_cursor) {
        int cx = x + 6 + textWidth(fld.buffer.substr(0, fld.cursor).c_str());
        drawRect(r, cx, y + 4, 2, h - 8, c_text);
    }
}

// ─── Slider ──────────────────────────────────────────────────
// Returns value 0..1 given mouse x in [bx, bx+bw].
static double sliderAt(int mx, int bx, int bw) {
    return std::max(0.0, std::min(1.0, (double)(mx - bx) / bw));
}

static void drawSlider(SDL_Renderer* r, int x, int y, int w, int val, int min, int max) {
    int h = 8;
    drawRect(r, x, y, w, h, c_field);
    double frac = (double)(val - min) / (max - min);
    int filled = (int)(frac * w);
    drawRect(r, x, y, filled, h, c_btn);
    drawBorder(r, x, y, w, h, c_dim);
    int handle_x = x + filled;
    drawRect(r, handle_x - 4, y - 3, 8, h + 6, c_btn_h);
}

// ──────────────────────────────────────────────────────────────
//  Theme constants for layout
// ──────────────────────────────────────────────────────────────
static const int BTN_W = 280;
static const int BTN_H = 50;
static const int FIELD_W = 360;

// ──────────────────────────────────────────────────────────────
//  Screen draw functions
// ──────────────────────────────────────────────────────────────

static void drawMenu(SDL_Renderer* r, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    int by = 100;

    // Title
    int title_w = textWidth("Romer", g_font_big);
    drawText(r, cx - title_w/2, 30, "Romer", c_text, g_font_big);

    // Subtitle
    const char* sub = "Screen mirror · File transfer · Remote shell";
    int sub_w = textWidth(sub);
    drawText(r, cx - sub_w/2, 60, sub, c_dim);

    int gap = 8;
    auto btn = [&](const char* label, int idx, SDL_Color bg, SDL_Color bg_h) -> bool {
        int bx = cx - BTN_W/2;
        int by_ = by + idx * (BTN_H + gap);
        bool over = (mx >= bx && mx < bx + BTN_W && my >= by_ && my < by_ + BTN_H);
        return button(r, {bx, by_, BTN_W, BTN_H, label, bg, bg_h}, over);
    };

    if (btn("  Start Server",          0, c_green, {70,210,90}))        st.screen = Screen::SERVER;
    if (btn("  Connect to Server...",   1, c_btn,   c_btn_h))            st.screen = Screen::CONNECT;
    if (btn("  Send File...",           2, c_btn,   c_btn_h))            st.screen = Screen::FILE_SEND;
    if (btn("  Receive File...",        3, c_btn,   c_btn_h))            st.screen = Screen::FILE_RECV;
    if (btn("  Remote Shell...",        4, c_btn,   c_btn_h))            st.screen = Screen::SHELL;
    if (btn("  Quit",                   5, c_red,   c_red_h))            st.running = false;

    // Status bar
    drawText(r, 10, st.window_h - 22, st.status_msg, c_dim);
}

// ─── Server screen ───────────────────────────────────────────
static void drawServer(SDL_Renderer* r, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;

    drawText(r, cx - textWidth("Server Settings", g_font_big)/2, 30,
             "Server Settings", c_text, g_font_big);

    int by = 100;
    int lx = cx - 200;
    int bx = cx - BTN_W/2;

    // FPS
    drawText(r, lx, by, "Max FPS:", c_text);
    drawSlider(r, lx + 100, by, 260, st.server_fps, 1, 60);
    char fps_s[16]; snprintf(fps_s, sizeof(fps_s), "%d", st.server_fps);
    drawText(r, lx + 370, by, fps_s, c_text);

    // Quality
    by += 35;
    drawText(r, lx, by, "Quality:", c_text);
    drawSlider(r, lx + 100, by, 260, st.server_quality, 10, 100);
    char q_s[16]; snprintf(q_s, sizeof(q_s), "%d%%", st.server_quality);
    drawText(r, lx + 370, by, q_s, c_text);

    // Start / Stop button
    by += 60;
    bool over = (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H);
    if (!st.server_active) {
        if (button(r, {bx, by, BTN_W, BTN_H, "  Start Server", c_green, {70,210,90}}, over)) {
            if (st.on_start_server) st.on_start_server(st.server_fps, st.server_quality);
            st.server_active = true;
            snprintf(st.status_msg, sizeof(st.status_msg),
                     "Server running on ports %d / %d / %d",
                     SCREEN_PORT, FILE_PORT, SHELL_PORT);
        }
    } else {
        if (button(r, {bx, by, BTN_W, BTN_H, "  Stop Server", c_red, c_red_h}, over)) {
            if (st.on_stop_server) st.on_stop_server();
            st.server_active = false;
            snprintf(st.status_msg, sizeof(st.status_msg), "Server stopped");
        }
    }

    // Status
    by += 60;
    drawText(r, lx, by, st.server_active ? "● Running" : "○ Stopped",
             st.server_active ? c_green : c_dim);

    // Back button
    by = st.window_h - 80;
    int bw = 120;
    int bbx = cx - bw/2;
    bool b_over = (mx >= bbx && mx < bbx + bw && my >= by && my < by + 40);
    if (button(r, {bbx, by, bw, 40, "Back", c_btn, c_btn_h}, b_over))
        st.screen = Screen::MENU;
}

// ─── Connect screen ──────────────────────────────────────────
static void drawConnect(SDL_Renderer* r, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;

    drawText(r, cx - textWidth("Connect to Server", g_font_big)/2, 30,
             "Connect to Server", c_text, g_font_big);

    int lx = cx - FIELD_W/2;
    int by = 100;

    drawText(r, lx, by, "Host:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.connect_host, true);

    by += 70;
    drawText(r, lx, by, "Port:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.connect_port, true);

    by += 80;
    int bx = cx - BTN_W/2;
    bool over = (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H);
    if (button(r, {bx, by, BTN_W, BTN_H, "  Connect", c_green, {70,210,90}}, over)) {
        const char* host = st.connect_host.buffer.empty() ? "127.0.0.1" : st.connect_host.buffer.c_str();
        int port = st.connect_port.buffer.empty() ? SCREEN_PORT : atoi(st.connect_port.buffer.c_str());
        if (st.on_connect) st.on_connect(host, port);
        st.viewer_active = true;
        st.screen = Screen::VIEWER;
    }

    // Back
    by = st.window_h - 80;
    int bw = 120, bbx = cx - bw/2;
    bool b_over = (mx >= bbx && mx < bbx + bw && my >= by && my < by + 40);
    if (button(r, {bbx, by, bw, 40, "Back", c_btn, c_btn_h}, b_over))
        st.screen = Screen::MENU;
}

// ─── Viewer screen ───────────────────────────────────────────
// The viewer is handled by main.cpp – this just draws a backdrop.
static void drawViewer(SDL_Renderer* r, UIState& st) {
    // Just instruction text (the actual video is drawn by main)
    drawRect(r, 0, 0, st.window_w, st.window_h, c_bg);
    drawText(r, 30, 30, "Viewing remote screen — ESC to disconnect", c_dim);
}

// ─── File Send screen ────────────────────────────────────────
static void drawFileSend(SDL_Renderer* r, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    drawText(r, cx - textWidth("Send File", g_font_big)/2, 30,
             "Send File", c_text, g_font_big);

    int lx = cx - FIELD_W/2;
    int by = 90;

    drawText(r, lx, by, "File path:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.fs_local, true);

    by += 70;
    drawText(r, lx, by, "Server host:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.fs_host, true);

    by += 80;
    int bx = cx - BTN_W/2;
    bool over = (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H);
    if (!st.fs_running) {
        if (button(r, {bx, by, BTN_W, BTN_H, "  Send File", c_green, {70,210,90}}, over)) {
            if (!st.fs_local.buffer.empty() && !st.fs_host.buffer.empty()) {
                st.fs_progress = 0;
                st.fs_running = true;
                if (st.on_file_send)
                    st.on_file_send(st.fs_local.buffer.c_str(),
                                    st.fs_host.buffer.c_str(), nullptr);
            } else {
                snprintf(st.status_msg, sizeof(st.status_msg), "Fill in all fields");
            }
        }
    } else {
        char pct[32]; snprintf(pct, sizeof(pct), "Sending... %.0f%%", st.fs_progress * 100);
        drawText(r, lx, by, pct, c_text);
        // Progress bar
        drawRect(r, lx, by + 30, (int)(FIELD_W * st.fs_progress), 8, c_green);
        drawBorder(r, lx, by + 30, FIELD_W, 8, c_dim);
    }

    by = st.window_h - 80;
    int bw = 120, bbx = cx - bw/2;
    bool b_over = (mx >= bbx && mx < bbx + bw && my >= by && my < by + 40);
    if (button(r, {bbx, by, bw, 40, "Back", c_btn, c_btn_h}, b_over))
        st.screen = Screen::MENU;
}

// ─── File Recv screen ────────────────────────────────────────
static void drawFileRecv(SDL_Renderer* r, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    drawText(r, cx - textWidth("Receive File", g_font_big)/2, 30,
             "Receive File", c_text, g_font_big);

    int lx = cx - FIELD_W/2;
    int by = 80;

    drawText(r, lx, by, "Remote path:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.fr_remote, true);
    by += 70;
    drawText(r, lx, by, "Save as:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.fr_local, true);
    by += 70;
    drawText(r, lx, by, "Server host:", c_text);
    drawField(r, lx, by + 20, FIELD_W, 36, st.fr_host, true);

    by += 80;
    int bx = cx - BTN_W/2;
    bool over = (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H);
    if (!st.fr_running) {
        if (button(r, {bx, by, BTN_W, BTN_H, "  Receive File", c_btn, c_btn_h}, over)) {
            if (!st.fr_remote.buffer.empty() && !st.fr_local.buffer.empty() &&
                !st.fr_host.buffer.empty()) {
                st.fr_progress = 0;
                st.fr_running = true;
                if (st.on_file_recv)
                    st.on_file_recv(st.fr_remote.buffer.c_str(),
                                    st.fr_local.buffer.c_str(),
                                    st.fr_host.buffer.c_str(), nullptr);
            } else {
                snprintf(st.status_msg, sizeof(st.status_msg), "Fill in all fields");
            }
        }
    } else {
        char pct[32]; snprintf(pct, sizeof(pct), "Receiving... %.0f%%", st.fr_progress * 100);
        drawText(r, lx, by, pct, c_text);
        drawRect(r, lx, by + 30, (int)(FIELD_W * st.fr_progress), 8, c_green);
        drawBorder(r, lx, by + 30, FIELD_W, 8, c_dim);
    }

    by = st.window_h - 80;
    int bw = 120, bbx = cx - bw/2;
    bool b_over = (mx >= bbx && mx < bbx + bw && my >= by && my < by + 40);
    if (button(r, {bbx, by, bw, 40, "Back", c_btn, c_btn_h}, b_over))
        st.screen = Screen::MENU;
}

// ─── Shell screen ────────────────────────────────────────────
static void drawShell(SDL_Renderer* r, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;

    if (!st.shell_active) {
        // Connect screen for shell
        drawText(r, cx - textWidth("Remote Shell", g_font_big)/2, 30,
                 "Remote Shell", c_text, g_font_big);

        int lx = cx - FIELD_W/2;
        int by = 100;
        drawText(r, lx, by, "Server host:", c_text);
        drawField(r, lx, by + 20, FIELD_W, 36, st.shell_host, true);

        by += 80;
        int bx = cx - BTN_W/2;
        bool over = (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H);
        if (button(r, {bx, by, BTN_W, BTN_H, "  Open Shell", c_green, {70,210,90}}, over)) {
            const char* host = st.shell_host.buffer.empty() ? "127.0.0.1" : st.shell_host.buffer.c_str();
            if (st.on_shell_connect) st.on_shell_connect(host);
            st.shell_active = true;
        }

        by = st.window_h - 80;
        int bw = 120, bbx = cx - bw/2;
        bool b_over = (mx >= bbx && mx < bbx + bw && my >= by && my < by + 40);
        if (button(r, {bbx, by, bw, 40, "Back", c_btn, c_btn_h}, b_over))
            st.screen = Screen::MENU;
        return;
    }

    // Terminal view
    drawRect(r, 0, 0, st.window_w, st.window_h, { 0, 0, 0, 255 });

    int term_h = st.window_h - 50;  // leave room for input bar
    int line_h = fontHeight(g_font_mono) + 2;
    int vis_lines = term_h / line_h;

    // Draw terminal lines (bottom-up, with scroll)
    int start_line = (int)st.term_buf.size() - vis_lines - st.term_scroll;
    if (start_line < 0) start_line = 0;

    int y = term_h - 10;
    for (size_t i = start_line; i < st.term_buf.size() && y > 0; i++) {
        y -= line_h;
        drawText(r, 8, y, st.term_buf[i].text.c_str(), c_text, g_font_mono);
    }

    // Input bar at bottom
    int ibar_h = 36;
    drawRect(r, 0, st.window_h - ibar_h, st.window_w, ibar_h, c_field);
    drawBorder(r, 0, st.window_h - ibar_h, st.window_w, ibar_h, c_dim);

    // Prompt + input text
    const char* prompt = "$ ";
    int pw = textWidth(prompt, g_font_mono);
    drawText(r, 8, st.window_h - ibar_h + (ibar_h - fontHeight(g_font_mono))/2,
             prompt, c_green, g_font_mono);
    int tx = 8 + pw;
    drawText(r, tx, st.window_h - ibar_h + (ibar_h - fontHeight(g_font_mono))/2,
             st.term_input.c_str(), c_text, g_font_mono);

    // Cursor
    int cw = textWidth(st.term_input.c_str(), g_font_mono);
    bool cursor_on = (SDL_GetTicks() / 500) % 2;
    if (cursor_on)
        drawRect(r, tx + cw, st.window_h - ibar_h + 6, 2, ibar_h - 12, c_text);

    // Shell output from other thread
    {
        std::lock_guard<std::mutex> lock(st.shell_mutex);
        for (auto& line : st.shell_output) {
            st.term_buf.push_back({line});
            if (st.term_buf.size() > (size_t)st.term_max)
                st.term_buf.pop_front();
        }
        st.shell_output.clear();
    }

    // Hint text
    drawText(r, 10, 4, "ESC to disconnect  |  Enter to send", c_dim);
}

// ──────────────────────────────────────────────────────────────
//  External API
// ──────────────────────────────────────────────────────────────

void ui_draw(SDL_Window* window, SDL_Renderer* renderer, UIState& state) {
    // Get window size
    SDL_GetWindowSize(window, &state.window_w, &state.window_h);

    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    // Clear
    drawRect(renderer, 0, 0, state.window_w, state.window_h, c_bg);

    switch (state.screen) {
        case Screen::MENU:      drawMenu(renderer, state, mx, my); break;
        case Screen::SERVER:    drawServer(renderer, state, mx, my); break;
        case Screen::CONNECT:   drawConnect(renderer, state, mx, my); break;
        case Screen::VIEWER:    drawViewer(renderer, state); break;
        case Screen::FILE_SEND: drawFileSend(renderer, state, mx, my); break;
        case Screen::FILE_RECV: drawFileRecv(renderer, state, mx, my); break;
        case Screen::SHELL:     drawShell(renderer, state, mx, my); break;
    }

    SDL_RenderPresent(renderer);
}

// ─── Event handling per-screen ───────────────────────────────
static void textFieldEvent(TextField& fld, const SDL_Event& ev) {
    if (!fld.active) return;
    if (ev.type == SDL_TEXTINPUT) {
        fld.buffer.insert(fld.cursor, ev.text.text);
        fld.cursor += (int)strlen(ev.text.text);
    } else if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_BACKSPACE && fld.cursor > 0) {
            fld.buffer.erase(fld.cursor - 1, 1);
            fld.cursor--;
        } else if (ev.key.keysym.sym == SDLK_DELETE && fld.cursor < (int)fld.buffer.size()) {
            fld.buffer.erase(fld.cursor, 1);
        } else if (ev.key.keysym.sym == SDLK_LEFT && fld.cursor > 0) {
            fld.cursor--;
        } else if (ev.key.keysym.sym == SDLK_RIGHT && fld.cursor < (int)fld.buffer.size()) {
            fld.cursor++;
        } else if (ev.key.keysym.sym == SDLK_HOME) {
            fld.cursor = 0;
        } else if (ev.key.keysym.sym == SDLK_END) {
            fld.cursor = (int)fld.buffer.size();
        }
    }
}

static bool isOver(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void menuEvents(SDL_Event& ev, UIState& st) {
    (void)ev; (void)st; // all clicks handled in draw function
}

static void serverEvents(SDL_Event& ev, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    if (ev.type == SDL_MOUSEBUTTONDOWN) {
        int lx = cx - 200;

        // FPS slider
        int sl_y = 100;
        if (isOver(mx, my, lx + 100, sl_y, 260, 8))
            st.server_fps = (int)(1 + sliderAt(mx, lx + 100, 260) * 59);

        // Quality slider
        sl_y = 135;
        if (isOver(mx, my, lx + 100, sl_y, 260, 8))
            st.server_quality = (int)(10 + sliderAt(mx, lx + 100, 260) * 90);
    }
}

static void connectEvents(SDL_Event& ev, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    int lx = cx - FIELD_W/2;

    if (ev.type == SDL_MOUSEBUTTONDOWN) {
        int by = 120;
        st.connect_host.active = isOver(mx, my, lx, by, FIELD_W, 36);
        by = 190;
        st.connect_port.active = isOver(mx, my, lx, by, FIELD_W, 36);
        if (!st.connect_host.active && !st.connect_port.active) {
            // Check connect button
            int bx = cx - BTN_W/2; by = 270;
            if (isOver(mx, my, bx, by, BTN_W, BTN_H)) {
                const char* host = st.connect_host.buffer.empty() ? "127.0.0.1" : st.connect_host.buffer.c_str();
                int port = st.connect_port.buffer.empty() ? SCREEN_PORT : atoi(st.connect_port.buffer.c_str());
                if (st.on_connect) st.on_connect(host, port);
                st.viewer_active = true;
                st.screen = Screen::VIEWER;
            }
        }
    } else {
        if (st.connect_host.active) textFieldEvent(st.connect_host, ev);
        if (st.connect_port.active) textFieldEvent(st.connect_port, ev);
    }
}

static void fileSendEvents(SDL_Event& ev, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    int lx = cx - FIELD_W/2;

    if (ev.type == SDL_MOUSEBUTTONDOWN) {
        int by = 110;
        st.fs_local.active  = isOver(mx, my, lx, by, FIELD_W, 36);
        by = 180;
        st.fs_host.active   = isOver(mx, my, lx, by, FIELD_W, 36);
    } else {
        if (st.fs_local.active) textFieldEvent(st.fs_local, ev);
        if (st.fs_host.active)  textFieldEvent(st.fs_host, ev);
    }
}

static void fileRecvEvents(SDL_Event& ev, UIState& st, int mx, int my) {
    int cx = st.window_w / 2;
    int lx = cx - FIELD_W/2;

    if (ev.type == SDL_MOUSEBUTTONDOWN) {
        int by = 100;
        st.fr_remote.active = isOver(mx, my, lx, by, FIELD_W, 36);
        by = 170;
        st.fr_local.active  = isOver(mx, my, lx, by, FIELD_W, 36);
        by = 240;
        st.fr_host.active   = isOver(mx, my, lx, by, FIELD_W, 36);
    } else {
        if (st.fr_remote.active) textFieldEvent(st.fr_remote, ev);
        if (st.fr_local.active)  textFieldEvent(st.fr_local, ev);
        if (st.fr_host.active)   textFieldEvent(st.fr_host, ev);
    }
}

static void shellEvents(SDL_Event& ev, UIState& st) {
    if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
            if (!st.term_input.empty()) {
                // Add to local display and send
                st.term_buf.push_back({"$ " + st.term_input});
                if (st.term_buf.size() > (size_t)st.term_max)
                    st.term_buf.pop_front();
                if (st.on_shell_input)
                    st.on_shell_input(st.term_input.c_str());
                st.term_input.clear();
            }
        } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
            if (!st.term_input.empty())
                st.term_input.pop_back();
        } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
            st.shell_active = false;
            st.screen = Screen::MENU;
        }
    } else if (ev.type == SDL_TEXTINPUT) {
        st.term_input += ev.text.text;
    }
}

void ui_handle_event(SDL_Window* window, SDL_Renderer* renderer,
                     SDL_Event& ev, UIState& state) {
    if (ev.type == SDL_QUIT) {
        state.running = false;
        return;
    }

    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    switch (state.screen) {
        case Screen::MENU:      menuEvents(ev, state); break;
        case Screen::SERVER:    serverEvents(ev, state, mx, my); break;
        case Screen::CONNECT:   connectEvents(ev, state, mx, my); break;
        case Screen::VIEWER:
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                state.screen = Screen::MENU;
            break;
        case Screen::FILE_SEND: fileSendEvents(ev, state, mx, my); break;
        case Screen::FILE_RECV: fileRecvEvents(ev, state, mx, my); break;
        case Screen::SHELL:     shellEvents(ev, state); break;
    }
}

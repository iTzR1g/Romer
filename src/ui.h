#pragma once

#include <cstdint>
#include <string>
#include <deque>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <functional>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;
struct TTF_Font;

enum class Screen {
    MENU, SERVER, CONNECT, VIEWER, FILE_SEND, FILE_RECV, SHELL,
};

struct TermLine { std::string text; };

struct TextField {
    std::string buffer;
    bool active = false;
    int cursor  = 0;
};

struct UIState {
    Screen screen = Screen::MENU;
    bool   running     = true;
    char   status_msg[256] = "Ready";
    int    window_w = 800, window_h = 600;

    // Server
    int  server_fps     = 30;
    int  server_quality = 85;
    bool server_active  = false;

    // Connect / viewer
    TextField connect_host;
    TextField connect_port;

    // File send
    TextField fs_local;
    TextField fs_host;
    double    fs_progress  = 0;
    bool      fs_running   = false;

    // File recv
    TextField fr_remote;
    TextField fr_local;
    TextField fr_host;
    double    fr_progress  = 0;
    bool      fr_running   = false;
    bool      viewer_active = false;

    // Shell
    TextField  shell_host;
    bool       shell_active  = false;
    std::deque<TermLine> term_buf;
    std::string          term_input;
    int                  term_scroll = 0;
    int                  term_max    = 500;

    // Thread-safe shell output queue
    std::mutex              shell_mutex;
    std::vector<std::string> shell_output;

    // Viewer texture (set/used by main loop)
    SDL_Texture* viewer_tex   = nullptr;
    int          viewer_w     = 0;
    int          viewer_h     = 0;
    bool         viewer_ok    = false;

    // Callbacks (set by main.cpp)
    std::function<void(int fps, int quality)>          on_start_server;
    std::function<void()>                               on_stop_server;
    std::function<void(const char* host, int port)>     on_connect;
    std::function<void(const char* host)>               on_shell_connect;
    std::function<void(const char* text)>               on_shell_input;
    std::function<void(const char* local_path, const char* host, void*)> on_file_send;
    std::function<void(const char* remote_path, const char* local_path, const char* host, void*)> on_file_recv;
};

bool  ui_init(SDL_Window* window, SDL_Renderer* renderer);
void  ui_shutdown();
void  ui_draw(SDL_Window* window, SDL_Renderer* renderer, UIState& state);
void  ui_handle_event(SDL_Window* window, SDL_Renderer* renderer,
                      SDL_Event& ev, UIState& state);
bool  ui_load_font(const char* path_override = nullptr);

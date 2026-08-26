// ─── Romer ───────────────────────────────────────────────────
//  One binary.  Graphical UI or headless CLI.  Your call.
//
//    romer                             graphical UI
//    romer --server [p] [fps] [q]      headless server
//    romer <host>                      quick-connect viewer
//    romer --shell <host>              remote shell
//    romer --send <f> <host>           push file
//    romer --recv <r> <l> <host>       pull file
// ──────────────────────────────────────────────────────────────

#include "capture.h"
#include "jpeg_utils.h"
#include "stream.h"
#include "file_transfer.h"
#include "remote_shell.h"
#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <vector>
#include <string>
#include <mutex>

#ifndef NO_SDL
#include <SDL2/SDL.h>
#endif

// ─── Platform socket helpers ─────────────────────────────────
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCK_CLOSE(s) closesocket(s)
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCK_CLOSE(s) ::close(s)
#endif

static int tcpConnect(const char* host, int port) {
#ifdef _WIN32
    static bool wsa_init = false;
    if (!wsa_init) { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); wsa_init = true; }
#endif
    int fd = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
#ifdef _WIN32
    addr.sin_addr.s_addr = inet_addr(host);
    if (addr.sin_addr.s_addr == INADDR_NONE) { SOCK_CLOSE(fd); return -1; }
#else
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) { SOCK_CLOSE(fd); return -1; }
#endif
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { SOCK_CLOSE(fd); return -1; }
    return fd;
}

// ─── Globals ──────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static void onSignal(int) { g_running = false; }

// ─── Forward declarations ─────────────────────────────────────
static int  runHeadlessServer(int port, int fps, int quality);
static int  runUI();
static int  viewMode(const char* host, int port);
static void printUsage(const char* prog);

// ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    if (argc < 2)
        return runUI();

    if (strcmp(argv[1], "--server") == 0) {
        int port    = (argc > 2) ? std::atoi(argv[2]) : SCREEN_PORT;
        int fps     = (argc > 3) ? std::atoi(argv[3]) : 30;
        int quality = (argc > 4) ? std::atoi(argv[4]) : 85;
        return runHeadlessServer(port, fps, quality);
    }

    if (strcmp(argv[1], "--send") == 0) {
        if (argc < 4) { printUsage(argv[0]); return 1; }
        FileTransferClient ft;
        if (!ft.connect(argv[3], FILE_PORT)) return 1;
        bool ok = ft.sendFile(argv[2]);
        return ok ? 0 : 1;
    }

    if (strcmp(argv[1], "--recv") == 0) {
        if (argc < 5) { printUsage(argv[0]); return 1; }
        FileTransferClient ft;
        if (!ft.connect(argv[4], FILE_PORT)) return 1;
        bool ok = ft.recvFile(argv[2], argv[3]);
        return ok ? 0 : 1;
    }

    if (strcmp(argv[1], "--shell") == 0) {
        const char* host; std::string sh;
        if (argc == 3)       host = argv[2];
        else if (argc == 4) { sh = argv[2]; host = argv[3]; }
        else { printUsage(argv[0]); return 1; }
        RemoteShellClient c; if (!c.connect(host, SHELL_PORT)) return 1;
        c.interactive(sh); return 0;
    }

    // Fallback: quick-connect viewer
    return viewMode(argv[1], (argc > 2) ? std::atoi(argv[2]) : SCREEN_PORT);
}

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Romer  —  LAN tools\n"
        "  %s                     graphical UI\n"
        "  %s --server            headless server\n"
        "  %s <host>              quick viewer\n"
        "  %s --shell <host>      remote shell\n"
        "  %s --send <f> <host>   push file\n"
        "  %s --recv <r> <l> <h>  pull file\n",
        prog, prog, prog, prog, prog, prog);
}

// ──────────────────────────────────────────────────────────────
//  Headless server
// ──────────────────────────────────────────────────────────────
static void screenCaptureThread(ScreenCapture* cap, int quality, int fps,
                                 StreamSender* sender,
                                 const std::atomic<bool>& run) {
    CaptureBuffer f; std::vector<uint8_t> jpg;
    auto dur = std::chrono::milliseconds(1000 / fps);
    while (run) {
        while (run && !sender->isConnected()) sender->acceptClient(200);
        if (!run) break;
        while (run && sender->isConnected()) {
            auto t0 = std::chrono::steady_clock::now();
            if (!cap->capture(f)) { fprintf(stderr,"[capture] failed\n"); break; }
            jpg.clear();
            jpeg::encode(f.data.data(), f.width, f.height, quality, jpg);
            if (!sender->sendFrame(f.width, f.height, jpg.data(), jpg.size())) break;
            auto el = std::chrono::steady_clock::now() - t0;
            if (dur - el > std::chrono::milliseconds(0)) std::this_thread::sleep_for(dur - el);
        }
        sender->disconnect();
    }
}

static int runHeadlessServer(int screen_port, int fps, int quality) {
    if (fps < 1 || fps > 120) fps = 30;
    if (quality < 1 || quality > 100) quality = 85;

    auto cap = std::unique_ptr<ScreenCapture>(createScreenCapture());
    if (!cap || !cap->init()) { fprintf(stderr,"[server] capture init failed\n"); return 1; }
    StreamSender ss;
    if (!ss.listen(screen_port)) return 1;

    FileTransferServer fs; bool fok = fs.listen(FILE_PORT);
    if (!fok) fprintf(stderr,"[server] file port %d failed\n", FILE_PORT);
    RemoteShellServer  sh; bool sok = sh.listen(SHELL_PORT);
    if (!sok) fprintf(stderr,"[server] shell port %d failed\n", SHELL_PORT);

    fprintf(stderr,"[server] %s  %d fps  quality %d\n", cap->backendName(), fps, quality);
    fprintf(stderr,"[server] ports: screen=%d  file=%d  shell=%d\n",
            screen_port, FILE_PORT, SHELL_PORT);

    std::thread scr_thr(screenCaptureThread, cap.get(), quality, fps, &ss, std::cref(g_running));
    std::thread file_thr([&](){ if(fok) while(g_running) fs.acceptAndHandle(200); });
    std::thread shell_thr([&](){ if(sok) while(g_running) sh.acceptAndHandle(200); });

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ss.close(); fs.close(); sh.close(); cap->shutdown();
    scr_thr.join(); file_thr.join(); shell_thr.join();
    fprintf(stderr,"[server] stopped\n");
    return 0;
}

// ──────────────────────────────────────────────────────────────
//  Quick-connect viewer (standalone window)
// ──────────────────────────────────────────────────────────────
#ifndef NO_SDL
static int viewMode(const char* host, int port) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr,"SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* w = nullptr; SDL_Renderer* r = nullptr; SDL_Texture* t = nullptr;

    auto mk = [&](int fw, int fh) {
        if (t) SDL_DestroyTexture(t); if (r) SDL_DestroyRenderer(r); if (w) SDL_DestroyWindow(w);
        w = SDL_CreateWindow("Romer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              fw, fh, SDL_WINDOW_RESIZABLE);
        r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, fw, fh);
        return t != nullptr;
    };

    StreamReceiver recv;
    if (!recv.connect(host, port)) { SDL_Quit(); return 1; }
    fprintf(stderr,"[view] connected  ESC/F: quit/fullscreen\n");

    int fw=0,fh=0; std::vector<uint8_t> rgb; bool fs=false, run=true;
    auto last_fps = std::chrono::steady_clock::now(); int frames=0;

    while (run) {
        SDL_Event e; while (SDL_PollEvent(&e)) {
            if (e.type==SDL_QUIT) run=false;
            if (e.type==SDL_KEYDOWN) {
                if (e.key.keysym.sym==SDLK_ESCAPE) run=false;
                if (e.key.keysym.sym==SDLK_f) { fs=!fs; SDL_SetWindowFullscreen(w, fs?SDL_WINDOW_FULLSCREEN_DESKTOP:0); }
            }
        }
        int nw=0,nh=0; std::vector<uint8_t> jpg;
        if (!recv.recvFrame(nw,nh,jpg)) break;
        if (nw!=fw||nh!=fh||!t) { fw=nw; fh=nh; mk(fw,fh); printf("[view] %dx%d\n",fw,fh); }
        rgb.clear(); int dw=0,dh=0;
        if (!jpeg::decode(jpg.data(),jpg.size(),rgb,dw,dh)) continue;
        SDL_UpdateTexture(t, nullptr, rgb.data(), fw*3);
        SDL_RenderClear(r); SDL_RenderCopy(r, t, nullptr, nullptr); SDL_RenderPresent(r);
        if (++frames%60==0) { auto n=std::chrono::steady_clock::now();
            double fps=60.0/std::chrono::duration<double>(n-last_fps).count();
            char buf[64]; snprintf(buf,sizeof(buf), "Romer  %dx%d  %.1f FPS",fw,fh,fps);
            SDL_SetWindowTitle(w,buf); last_fps=n; frames=0; }
    }
    if (t) SDL_DestroyTexture(t); if (r) SDL_DestroyRenderer(r); if (w) SDL_DestroyWindow(w);
    SDL_Quit(); return 0;
}
#else
static int viewMode(const char*, int) {
    fprintf(stderr, "[romer] SDL not available – viewer disabled.\n");
    return 1;
}
#endif

// ──────────────────────────────────────────────────────────────
//  Graphical UI mode
// ──────────────────────────────────────────────────────────────
#ifdef HAS_TTF
#include "ui.h"
#include <SDL2/SDL_ttf.h>

static int runUI() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[ui] SDL_Init: %s\n", SDL_GetError());
        fprintf(stderr, "[ui] Try --server for headless mode, or install SDL2\n");
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("Romer",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        800, 600, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr,"[ui] SDL_CreateWindow: %s\n",SDL_GetError()); SDL_Quit(); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) { fprintf(stderr,"[ui] SDL_CreateRenderer: %s\n",SDL_GetError()); SDL_Quit(); return 1; }

    if (!ui_init(win, ren)) {
        fprintf(stderr, "[ui] TTF_Init failed – install SDL2_ttf\n");
        fprintf(stderr, "[ui] Try --server for headless mode, or install libsdl2-ttf-dev\n");
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }

    // ── Server state (owned here, accessed by callbacks) ──

    // ── Server state (owned here, accessed by callbacks) ──
    struct {
        std::unique_ptr<ScreenCapture> capturer;
        StreamSender  screen_sender;
        FileTransferServer file_srv;
        RemoteShellServer  shell_srv;
        std::atomic<bool>  running{false};
        std::thread screen_thr, file_thr, shell_thr;
    } srv;

    struct {
        StreamReceiver recv;
        std::vector<uint8_t> rgb;
        std::thread reader_thr;
        std::atomic<bool> connected{false};
    } view;

    struct {
        int sock = -1;
        std::thread reader;
        std::atomic<bool> ok{false};
    } sh;

    // ── UI state with callbacks ──
    UIState st;

    st.on_start_server = [&](int fps, int quality) {
        if (srv.running) return;
        srv.capturer.reset(createScreenCapture());
        if (!srv.capturer || !srv.capturer->init()) {
            snprintf(st.status_msg, sizeof(st.status_msg), "Capture init failed");
            return;
        }
        if (!srv.screen_sender.listen(SCREEN_PORT)) {
            snprintf(st.status_msg, sizeof(st.status_msg), "Failed to listen on port %d", SCREEN_PORT);
            return;
        }
        bool fok = srv.file_srv.listen(FILE_PORT);
        bool sok = srv.shell_srv.listen(SHELL_PORT);
        srv.running = true;

        srv.screen_thr = std::thread(screenCaptureThread, srv.capturer.get(),
                                      quality, fps, &srv.screen_sender,
                                      std::cref(srv.running));
        srv.file_thr  = std::thread([&](){ while(srv.running) srv.file_srv.acceptAndHandle(200); });
        srv.shell_thr = std::thread([&](){ while(srv.running) srv.shell_srv.acceptAndHandle(200); });

        snprintf(st.status_msg, sizeof(st.status_msg),
                 "Server: ports %d/%d/%d  (%s)", SCREEN_PORT, FILE_PORT, SHELL_PORT,
                 srv.capturer->backendName());
    };

    st.on_stop_server = [&]() {
        if (!srv.running) return;
        srv.running = false;
        srv.screen_sender.close();
        srv.file_srv.close();
        srv.shell_srv.close();
        srv.screen_thr.join();
        srv.file_thr.join();
        srv.shell_thr.join();
        srv.capturer->shutdown();
        snprintf(st.status_msg, sizeof(st.status_msg), "Server stopped");
    };

    st.on_connect = [&](const char* host, int port) {
        if (view.connected) return;
        if (!view.recv.connect(host, port)) {
            snprintf(st.status_msg, sizeof(st.status_msg), "Connect to %s:%d failed", host, port);
            st.screen = Screen::MENU;
            st.viewer_ok = false;
            return;
        }
        view.connected = true;
        snprintf(st.status_msg, sizeof(st.status_msg), "Viewing %s:%d", host, port);
    };

    st.on_shell_connect = [&](const char* host) {
        sh.sock = tcpConnect(host, SHELL_PORT);
        if (sh.sock < 0) {
            snprintf(st.status_msg, sizeof(st.status_msg), "Shell connect to %s failed", host);
            st.shell_active = false;
            return;
        }
        // Send empty shell path
        uint16_t zero = 0;
        ::send(sh.sock, (const char*)&zero, 2, 0);
        uint8_t resp;
        if (::recv(sh.sock, (char*)&resp, 1, 0) != 1 || resp != 0) {
            snprintf(st.status_msg, sizeof(st.status_msg), "Shell rejected");
            SOCK_CLOSE(sh.sock); sh.sock = -1; st.shell_active = false;
            return;
        }
        sh.ok = true;
        st.term_buf.clear();
        st.term_input.clear();

        // Reader thread
        sh.reader = std::thread([&]() {
            uint8_t buf[65536];
            while (sh.ok) {
                int n = ::recv(sh.sock, (char*)buf, sizeof(buf), 0);
                if (n <= 0) { sh.ok = false; break; }
                // Split into lines and push to UI
                std::lock_guard<std::mutex> lk(st.shell_mutex);
                std::string accum((const char*)buf, n);
                st.shell_output.push_back(accum);
            }
        });
        snprintf(st.status_msg, sizeof(st.status_msg), "Shell connected to %s", host);
    };

    st.on_shell_input = [&](const char* text) {
        if (sh.sock >= 0) {
            std::string line = text;
            line += "\n";
            ::send(sh.sock, line.data(), line.size(), 0);
        }
    };

    // ── Main loop ──
    while (st.running) {
        // Events
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            ui_handle_event(win, ren, ev, st);

        // ── Viewer mode: receive + decode frame ──
        if (st.screen == Screen::VIEWER && view.connected) {
            int nw = 0, nh = 0;
            std::vector<uint8_t> jpg;
            if (!view.recv.recvFrame(nw, nh, jpg)) {
                snprintf(st.status_msg, sizeof(st.status_msg), "Viewer disconnected");
                view.connected = false;
                st.screen = Screen::MENU;
                st.viewer_ok = false;
            } else {
                // Decode
                view.rgb.clear();
                int dw = 0, dh = 0;
                if (jpeg::decode(jpg.data(), jpg.size(), view.rgb, dw, dh)) {
                    // Create or resize texture
                    if (nw != st.viewer_w || nh != st.viewer_h || !st.viewer_tex) {
                        if (st.viewer_tex) SDL_DestroyTexture(st.viewer_tex);
                        st.viewer_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
                                                           SDL_TEXTUREACCESS_STREAMING, nw, nh);
                        st.viewer_w = nw; st.viewer_h = nh;
                    }
                    if (st.viewer_tex) {
                        SDL_UpdateTexture(st.viewer_tex, nullptr, view.rgb.data(), nw * 3);
                        st.viewer_ok = true;
                    }
                }
            }
        }

        // ── Draw ──
        if (st.screen == Screen::VIEWER && st.viewer_ok) {
            // Full-window video
            SDL_RenderClear(ren);
            float aspect = (float)st.viewer_w / st.viewer_h;
            int dw = st.window_w, dh = st.window_h;
            if ((float)dw / dh > aspect) { dw = (int)(dh * aspect); }
            else                         { dh = (int)(dw / aspect); }
            SDL_Rect dst = { (st.window_w - dw)/2, (st.window_h - dh)/2, dw, dh };
            SDL_RenderCopy(ren, st.viewer_tex, nullptr, &dst);
            // Overlay hint
            ui_draw(win, ren, st);
        } else {
            ui_draw(win, ren, st);
        }

        SDL_Delay(10);  // ~100 FPS max
    }

    // ── Cleanup ──
    if (srv.running) {
        srv.running = false;
        srv.screen_sender.close(); srv.file_srv.close(); srv.shell_srv.close();
        srv.screen_thr.join(); srv.file_thr.join(); srv.shell_thr.join();
        srv.capturer->shutdown();
    }
    if (sh.ok) { sh.ok = false; if (sh.sock >= 0) { SOCK_CLOSE(sh.sock); sh.sock = -1; } sh.reader.join(); }
    if (st.viewer_tex) SDL_DestroyTexture(st.viewer_tex);

    ui_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#else
static int runUI() {
    fprintf(stderr, "[romer] Graphical UI requires SDL2 + SDL2_ttf.\n");
    fprintf(stderr, "[romer] Use --server, --send, --recv or --shell for CLI mode.\n");
    return 1;
}
#endif

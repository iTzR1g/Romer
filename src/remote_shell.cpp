#include "remote_shell.h"
#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <thread>
#include <atomic>
#include <chrono>

// ─── Platform socket / OS glue ─────────────────────────────
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    using SOCK_T = SOCKET;
    #define SOCK_CLOSE(s)        closesocket(s)
    #define SOCK_ERR_VAL         INVALID_SOCKET
    #define IS_VALID_SOCK(s)     (s != INVALID_SOCKET)
    #define SOCK_READ(fd,buf,len) recv(fd,(char*)(buf),(int)(len),0)
    #define SOCK_WRITE(fd,buf,len) send(fd,(const char*)(buf),(int)(len),0)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <termios.h>
    #include <signal.h>
    #include <sys/wait.h>
    #if defined(__APPLE__)
        #include <util.h>
    #else
        #include <pty.h>
    #endif
    using SOCK_T = int;
    #define SOCK_CLOSE(s)        ::close(s)
    #define SOCK_ERR_VAL         (-1)
    #define IS_VALID_SOCK(s)     (s >= 0)
    #define SOCK_READ(fd,buf,len) ::read(fd,buf,len)
    #define SOCK_WRITE(fd,buf,len) ::write(fd,buf,len)
    static constexpr int INVALID_SOCKET = -1;
#endif

// ─── Socket helpers ────────────────────────────────────────
static bool wsaStart(bool* flag) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2,2), &w) != 0) return false;
    *flag = true;
#endif
    (void)flag; return true;
}
static void wsaStop(bool flag) {
#ifdef _WIN32
    if (flag) WSACleanup();
#endif
    (void)flag;
}

static bool recvAll(SOCK_T fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    while (len > 0) {
        int n = SOCK_READ(fd, p, len);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static bool sendAll(SOCK_T fd, const void* buf, size_t len) {
    auto* p = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        int n = SOCK_WRITE(fd, p, len);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────
//  RemoteShellServer
// ──────────────────────────────────────────────────────────────
RemoteShellServer::~RemoteShellServer() { close(); }

bool RemoteShellServer::listen(int port) {
    if (!wsaStart(&owns_wsa_)) return false;
    listen_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
    if (::listen(listen_fd_, 1) < 0) return false;

    fprintf(stderr, "[shell] Listening on port %d\n", port);
    return true;
}

void RemoteShellServer::acceptAndHandle(int timeout_ms) {
    if (listen_fd_ < 0) return;

#ifdef _WIN32
    fd_set rfds; FD_ZERO(&rfds); FD_SET(listen_fd_, &rfds);
    timeval tv{ timeout_ms/1000, (timeout_ms%1000)*1000 };
    if (select(0, &rfds, nullptr, nullptr, &tv) <= 0) return;
    sockaddr_in cli{}; socklen_t clen = sizeof(cli);
    SOCK_T fd = ::accept(listen_fd_, (sockaddr*)&cli, &clen);
    if (!IS_VALID_SOCK(fd)) return;
#else
    pollfd pfd{ listen_fd_, POLLIN, 0 };
    if (poll(&pfd, 1, timeout_ms) <= 0) return;
    sockaddr_in cli{}; socklen_t clen = sizeof(cli);
    SOCK_T fd = ::accept(listen_fd_, (sockaddr*)&cli, &clen);
    if (!IS_VALID_SOCK(fd)) return;
#endif

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
    fprintf(stderr, "[shell] Client connected: %s\n", ip);

    int r = handleClient((int)fd);
    if (r != 0)
        fprintf(stderr, "[shell] Session ended with code %d\n", r);
    SOCK_CLOSE(fd);
    fprintf(stderr, "[shell] Client disconnected\n");
}

void RemoteShellServer::close() {
    if (listen_fd_ >= 0) { SOCK_CLOSE(listen_fd_); listen_fd_ = -1; }
    wsaStop(owns_wsa_); owns_wsa_ = false;
}

// ─── POSIX: forkpty + relay loop ──────────────────────────────
#ifndef _WIN32
static int relayLoopPosix(int client_fd, const std::string& shell_path) {
    // Spawn shell in a PTY so it behaves like a real terminal
    // (colors, tab-completion, signals, etc.).
    int master;
    pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
    if (pid == -1) { perror("forkpty"); return -1; }

    if (pid == 0) {
        // ── child ──
        setenv("TERM", "xterm-256color", 1);
        // Use "sh -c" so we accept any shell path (including args).
        execl("/bin/sh", "sh", "-c", shell_path.c_str(), nullptr);
        _exit(1);
    }

    // ── parent: relay data between socket and PTY ──
    uint8_t ok = 0;
    sendAll(client_fd, &ok, 1);
    fprintf(stderr, "[shell] Spawned PID %d  (%s)\n", pid, shell_path.c_str());

    std::atomic<bool> running{true};

    // Reader thread: PTY → socket
    std::thread reader([&]() {
        uint8_t buf[65536];
        while (running) {
            int n = read(master, buf, sizeof(buf));
            if (n <= 0) { running = false; break; }
            if (!sendAll(client_fd, buf, n)) { running = false; break; }
        }
        close(master);
    });

    // Main thread: socket → PTY
    {
        uint8_t buf[65536];
        while (running) {
            int n = SOCK_READ(client_fd, buf, sizeof(buf));
            if (n <= 0) { running = false; break; }
            if (write(master, buf, n) < 0 && errno == EIO) {
                // PTY child has exited
                running = false;
                break;
            }
        }
    }

    running = false;
    reader.join();

    // Kill the shell if still alive
    kill(pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);
    return 0;
}
#endif

// ─── Windows: pipes + threads ─────────────────────────────────
// NOTE: stderr is redirected to stdout in the child to avoid
//       two threads writing to the same socket concurrently.
#ifdef _WIN32
static int relayLoopWindows(int client_fd, const std::string& shell_path) {
    HANDLE stdin_rd  = nullptr;
    HANDLE stdin_wr  = nullptr;
    HANDLE stdout_rd = nullptr;
    HANDLE stdout_wr = nullptr;

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

    CreatePipe(&stdin_rd,  &stdin_wr,  &sa, 0);
    CreatePipe(&stdout_rd, &stdout_wr, &sa, 0);

    SetHandleInformation(stdin_wr,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.hStdInput  = stdin_rd;
    si.hStdOutput = stdout_wr;
    si.hStdError  = stdout_wr;      // stderr → same pipe as stdout
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::string cmdline = shell_path.empty()
        ? "cmd.exe"
        : (std::string("cmd.exe /c \"") + shell_path + "\"");
    char* cmd_mut = _strdup(cmdline.c_str());

    BOOL ok = CreateProcessA(nullptr, cmd_mut, nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    free(cmd_mut);

    CloseHandle(stdin_rd);
    CloseHandle(stdout_wr);

    if (!ok) {
        CloseHandle(stdin_wr);
        CloseHandle(stdout_rd);
        return -1;
    }

    CloseHandle(pi.hThread);

    uint8_t resp = 0;
    sendAll(client_fd, &resp, 1);

    std::atomic<bool> running{true};

    // Thread: read from socket → write to child's stdin
    std::thread in_thread([&]() {
        uint8_t buf[65536];
        while (running) {
            int n = SOCK_READ(client_fd, buf, sizeof(buf));
            if (n <= 0) { running = false; break; }
            DWORD written;
            WriteFile(stdin_wr, buf, n, &written, nullptr);
        }
        CloseHandle(stdin_wr);
    });

    // Thread: read from child's stdout/stderr → write to socket
    std::thread out_thread([&]() {
        uint8_t buf[65536];
        while (running) {
            DWORD n = 0;
            if (!ReadFile(stdout_rd, buf, sizeof(buf), &n, nullptr) || n == 0)
                { running = false; break; }
            if (!sendAll(client_fd, buf, n)) { running = false; break; }
        }
        CloseHandle(stdout_rd);
    });

    WaitForSingleObject(pi.hProcess, INFINITE);
    running = false;

    CloseHandle(stdout_rd);
    CloseHandle(stdin_wr);

    in_thread.join();
    out_thread.join();

    CloseHandle(pi.hProcess);
    return 0;
}
#endif

int RemoteShellServer::handleClient(int fd) {
    // ── Read shell path from client ──
    uint16_t plen;
    if (!recvAll(fd, &plen, 2)) return -1;
    plen = ntohs(plen);
    if (plen >= SHELL_PATH_MAX) return -1;

    std::string shell_path;
    if (plen > 0) {
        shell_path.resize(plen);
        if (!recvAll(fd, &shell_path[0], plen)) return -1;
    }

    // Default shell per platform
    if (shell_path.empty()) {
#ifdef _WIN32
        shell_path = "cmd.exe";
#else
        // Try common shell locations across distros
        static const char* shells[] = {
            "/bin/bash",        // Debian / Ubuntu / Arch / Gentoo
            "/usr/bin/bash",    // Fedora 33+ / NixOS / Void
            "/bin/sh",          // POSIX fallback (Alpine / minimal installs)
            "/usr/bin/sh",
        };
        shell_path = "/bin/sh";  // final fallback
        for (auto s : shells) {
            if (access(s, X_OK) == 0) { shell_path = s; break; }
        }
#endif
    }

    fprintf(stderr, "[shell] Starting: %s\n", shell_path.c_str());

#ifdef _WIN32
    return relayLoopWindows(fd, shell_path);
#else
    return relayLoopPosix(fd, shell_path);
#endif
}

// ──────────────────────────────────────────────────────────────
//  RemoteShellClient
// ──────────────────────────────────────────────────────────────
RemoteShellClient::~RemoteShellClient() { disconnect(); }

bool RemoteShellClient::connect(const std::string& host, int port) {
    if (!wsaStart(&owns_wsa_)) return false;
    sock_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        fprintf(stderr, "[shell] Bad address: %s\n", host.c_str());
        return false;
    }
    if (::connect(sock_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[shell] Connect to %s:%d failed\n", host.c_str(), port);
        return false;
    }
    return true;
}

void RemoteShellClient::disconnect() {
    if (sock_fd_ >= 0) { SOCK_CLOSE(sock_fd_); sock_fd_ = -1; }
    wsaStop(owns_wsa_); owns_wsa_ = false;
}

// ─── Terminal raw mode helpers (POSIX) ────────────────────────
#ifndef _WIN32
static termios g_orig_termios;
static bool    g_termios_saved = false;

static void restoreTerm() {
    if (g_termios_saved)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    g_termios_saved = false;
}

static void setRawTerm() {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    g_termios_saved = true;
    atexit(restoreTerm);

    termios raw = g_orig_termios;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                     | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
#endif

// ─── Interactive shell session ────────────────────────────────
void RemoteShellClient::interactive(const std::string& shell_path) {
    // ── Send shell path ──
    uint16_t plen = (uint16_t)shell_path.size();
    plen = htons(plen);
    sendAll(sock_fd_, &plen, 2);
    if (!shell_path.empty())
        sendAll(sock_fd_, shell_path.data(), shell_path.size());

    // ── Wait for server OK ──
    uint8_t status;
    if (!recvAll(sock_fd_, &status, 1) || status != 0) {
        fprintf(stderr, "[shell] Server refused\n");
        return;
    }

    fprintf(stderr, "[shell] Connected.  Ctrl-C / Ctrl-D to exit.\n");

#ifdef _WIN32
    // Windows: no raw mode needed, just use threads
    std::atomic<bool> running{true};

    std::thread reader([&]() {
        uint8_t buf[65536];
        while (running) {
            int n = SOCK_READ(sock_fd_, buf, sizeof(buf));
            if (n <= 0) { running = false; break; }
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }
    });

    {
        uint8_t buf[65536];
        while (running) {
            int c = getchar();
            if (c == EOF) { running = false; break; }
            buf[0] = (uint8_t)c;
            if (!sendAll(sock_fd_, buf, 1)) break;
        }
    }

    running = false;
    reader.join();
#else
    // POSIX: raw mode + poll() on stdin and socket
    setRawTerm();

    // Socket → stdout reader thread
    std::atomic<bool> running{true};

    std::thread reader([&]() {
        uint8_t buf[65536];
        while (running) {
            int n = SOCK_READ(sock_fd_, buf, sizeof(buf));
            if (n <= 0) { running = false; break; }
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }
    });

    // stdin → socket
    {
        uint8_t buf[65536];
        while (running) {
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) { running = false; break; }
            if (!sendAll(sock_fd_, buf, n)) { running = false; break; }
        }
    }

    running = false;
    reader.join();
    restoreTerm();
#endif
}

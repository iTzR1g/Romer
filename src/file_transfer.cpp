#include "file_transfer.h"
#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

// ─── Platform socket glue (same pattern as stream.cpp) ─────
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SOCK_T = SOCKET;
    #define SOCK_CLOSE(s)        closesocket(s)
    #define SOCK_ERR_VAL         INVALID_SOCKET
    #define IS_VALID_SOCK(s)     (s != INVALID_SOCKET)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    using SOCK_T = int;
    #define SOCK_CLOSE(s)        ::close(s)
    #define SOCK_ERR_VAL         (-1)
    #define IS_VALID_SOCK(s)     (s >= 0)
    static constexpr int INVALID_SOCKET = -1;
#endif

// ─── Helpers ──────────────────────────────────────────────────
static bool wsaStart(bool* flag) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2,2), &w) != 0) return false;
    *flag = true;
#endif
    (void)flag;
    return true;
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
        int n = recv(fd, reinterpret_cast<char*>(p), (int)len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static bool sendAll(SOCK_T fd, const void* buf, size_t len) {
    auto* p = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        int n = send(fd, reinterpret_cast<const char*>(p), (int)len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static uint16_t r16(const uint8_t* d) {
    return (uint16_t)((uint16_t)d[0]<<8 | d[1]);
}
static uint32_t r32(const uint8_t* d) {
    return (uint32_t)((uint32_t)d[0]<<24 | (uint32_t)d[1]<<16 |
                      (uint32_t)d[2]<<8  | d[3]);
}
static uint64_t r64(const uint8_t* d) {
    return ((uint64_t)r32(d) << 32) | r32(d+4);
}
static void w16(uint8_t* d, uint16_t v) { d[0]=v>>8; d[1]=v; }
static void w32(uint8_t* d, uint32_t v) { d[0]=v>>24; d[1]=v>>16; d[2]=v>>8; d[3]=v; }
static void w64(uint8_t* d, uint64_t v) { w32(d, v>>32); w32(d+4, (uint32_t)v); }

static constexpr int HEAD_SZ = 4;  // max header we send (direction+name+size)

// ──────────────────────────────────────────────────────────────
//  FileTransferServer
// ──────────────────────────────────────────────────────────────
FileTransferServer::~FileTransferServer() { close(); }

bool FileTransferServer::listen(int port) {
    if (!wsaStart(&owns_wsa_)) return false;
    listen_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port   = htons((uint16_t)port);

    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
    if (::listen(listen_fd_, 1) < 0) return false;

    fprintf(stderr, "[file] Listening on port %d\n", port);
    return true;
}

void FileTransferServer::acceptAndHandle(int timeout_ms) {
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
    fprintf(stderr, "[file] Client connected: %s\n", ip);

    int r = handleClient((int)fd);
    if (r != 0)
        fprintf(stderr, "[file] Transfer failed with code %d\n", r);
    SOCK_CLOSE(fd);
    fprintf(stderr, "[file] Client done\n");
}

void FileTransferServer::close() {
    if (listen_fd_ >= 0) { SOCK_CLOSE(listen_fd_); listen_fd_ = -1; }
    wsaStop(owns_wsa_); owns_wsa_ = false;
}

// ─── Read the client's request header ─────────────────────────
// Wire: [1 dir][2 name_len][name][8 file_size (push only)]
int FileTransferServer::handleClient(int fd) {
    uint8_t dir;
    if (!recvAll(fd, &dir, 1)) return -1;

    uint8_t nlen_b[2];
    if (!recvAll(fd, nlen_b, 2)) return -1;
    uint16_t nlen = r16(nlen_b);
    if (nlen > 4096) return -1;

    std::string filename(nlen, '\0');
    if (!recvAll(fd, &filename[0], nlen)) return -1;

    if (dir == FILE_PUSH) {
        uint8_t szb[8];
        if (!recvAll(fd, szb, 8)) return -1;
        uint64_t fsize = r64(szb);
        return handlePush(fd, filename, fsize);
    } else if (dir == FILE_PULL) {
        return handlePull(fd, filename);
    }
    return -1;
}

int FileTransferServer::handlePush(int fd, const std::string& filename, uint64_t total) {
    FILE* f = fopen(filename.c_str(), "wb");
    if (!f) {
        uint8_t resp[3] = { 1, 0, 0 };  // status=ERROR, msg_len=0
        sendAll(fd, resp, 3);
        return -1;
    }

    // Send OK
    uint8_t ok = 0;
    sendAll(fd, &ok, 1);

    uint64_t received = 0;
    while (received < total) {
        uint8_t hdr[4];
        if (!recvAll(fd, hdr, 4)) { fclose(f); return -1; }
        uint32_t chunk = r32(hdr);
        if (chunk == 0) break;  // premature end
        if (received + chunk > total) chunk = (uint32_t)(total - received);

        uint8_t buf[FILE_CHUNK_SIZE];
        if (!recvAll(fd, buf, chunk)) { fclose(f); return -1; }
        if (fwrite(buf, 1, chunk, f) != chunk) { fclose(f); return -1; }
        received += chunk;
    }

    fclose(f);

    uint8_t fin = 0;  // SUCCESS
    sendAll(fd, &fin, 1);

    fprintf(stderr, "[file] Received %s  (%llu bytes)\n",
            filename.c_str(), (unsigned long long)received);
    return 0;
}

int FileTransferServer::handlePull(int fd, const std::string& filename) {
    FILE* f = fopen(filename.c_str(), "rb");
    if (!f) {
        uint8_t resp[3] = { 1, 0, 0 };
        sendAll(fd, resp, 3);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    uint64_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t meta[9] = {};
    meta[0] = 0;  // OK
    w64(meta+1, fsize);
    sendAll(fd, meta, 9);

    uint8_t buf[FILE_CHUNK_SIZE];
    uint64_t sent = 0;
    while (sent < fsize) {
        uint32_t now = (uint32_t)(fsize - sent < FILE_CHUNK_SIZE ?
                                   fsize - sent : FILE_CHUNK_SIZE);
        if (fread(buf, 1, now, f) != now) { fclose(f); return -1; }

        uint8_t hdr[4];
        w32(hdr, now);
        if (!sendAll(fd, hdr, 4)) { fclose(f); return -1; }
        if (!sendAll(fd, buf, now)) { fclose(f); return -1; }

        sent += now;
    }

    uint8_t done[4] = {};
    sendAll(fd, done, 4);  // zero-size chunk = done
    fclose(f);

    uint8_t fin;
    if (!recvAll(fd, &fin, 1)) return -1;

    fprintf(stderr, "[file] Sent %s  (%llu bytes)\n",
            filename.c_str(), (unsigned long long)sent);
    return fin == 0 ? 0 : -1;
}

// ──────────────────────────────────────────────────────────────
//  FileTransferClient
// ──────────────────────────────────────────────────────────────
FileTransferClient::~FileTransferClient() { disconnect(); }

bool FileTransferClient::connect(const std::string& host, int port) {
    if (!wsaStart(&owns_wsa_)) return false;
    sock_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        fprintf(stderr, "[file] Bad address: %s\n", host.c_str());
        return false;
    }
    if (::connect(sock_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[file] Connect to %s:%d failed\n", host.c_str(), port);
        return false;
    }
    return true;
}

void FileTransferClient::disconnect() {
    if (sock_fd_ >= 0) { SOCK_CLOSE(sock_fd_); sock_fd_ = -1; }
    wsaStop(owns_wsa_); owns_wsa_ = false;
}

bool FileTransferClient::sendFile(const std::string& local_path) {
    FILE* f = fopen(local_path.c_str(), "rb");
    if (!f) { perror("fopen"); return false; }

    fseek(f, 0, SEEK_END);
    uint64_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Extract filename from path
    auto pos = local_path.find_last_of("/\\");
    std::string fname = (pos == std::string::npos) ? local_path : local_path.substr(pos+1);

    // Build header on the stack
    uint8_t hdr[1 + 2 + 256 + 8];
    size_t  fnl = fname.size();
    hdr[0] = FILE_PUSH;
    w16(hdr+1, (uint16_t)fnl);
    memcpy(hdr+3, fname.data(), fnl);
    w64(hdr+3+fnl, fsize);

    if (!sendAll(sock_fd_, hdr, 1+2+fnl+8)) { fclose(f); return false; }

    uint8_t status;
    if (!recvAll(sock_fd_, &status, 1)) { fclose(f); return false; }
    if (status != 0) { fprintf(stderr, "[file] Server refused\n"); fclose(f); return false; }

    uint8_t buf[FILE_CHUNK_SIZE];
    uint64_t total = 0;
    while (total < fsize) {
        uint32_t now = (uint32_t)(fsize - total < FILE_CHUNK_SIZE ?
                                   fsize - total : FILE_CHUNK_SIZE);
        if (fread(buf, 1, now, f) != now) { fclose(f); return false; }

        uint8_t ch[4]; w32(ch, now);
        if (!sendAll(sock_fd_, ch, 4)) { fclose(f); return false; }
        if (!sendAll(sock_fd_, buf, now)) { fclose(f); return false; }
        total += now;
    }

    fclose(f);

    uint8_t done[4] = {};
    sendAll(sock_fd_, done, 4);

    uint8_t fin;
    if (!recvAll(sock_fd_, &fin, 1)) return false;

    fprintf(stderr, "[file] Uploaded %s (%llu bytes)  %s\n",
            fname.c_str(), (unsigned long long)fsize,
            fin == 0 ? "OK" : "FAIL");
    return fin == 0;
}

bool FileTransferClient::recvFile(const std::string& remote_path,
                                   const std::string& local_path) {
    FILE* f = fopen(local_path.c_str(), "wb");
    if (!f) { perror("fopen"); return false; }

    auto pos = remote_path.find_last_of("/\\");
    std::string fname = (pos == std::string::npos) ? remote_path : remote_path.substr(pos+1);

    uint8_t hdr[1 + 2 + 256];
    size_t fnl = fname.size();
    hdr[0] = FILE_PULL;
    w16(hdr+1, (uint16_t)fnl);
    memcpy(hdr+3, fname.data(), fnl);

    if (!sendAll(sock_fd_, hdr, 1+2+fnl)) { fclose(f); return false; }

    uint8_t resp[9];
    if (!recvAll(sock_fd_, resp, 9)) { fclose(f); return false; }
    if (resp[0] != 0) { fprintf(stderr, "[file] Server cannot send file\n"); fclose(f); return false; }

    uint64_t fsize = r64(resp+1);
    uint64_t total = 0;

    while (total < fsize) {
        uint8_t ch[4];
        if (!recvAll(sock_fd_, ch, 4)) { fclose(f); return false; }
        uint32_t now = r32(ch);
        if (now == 0) break;

        uint8_t buf[FILE_CHUNK_SIZE];
        if (!recvAll(sock_fd_, buf, now)) { fclose(f); return false; }
        if (fwrite(buf, 1, now, f) != now) { fclose(f); return false; }
        total += now;
    }

    fclose(f);

    uint8_t fin = 0;
    sendAll(sock_fd_, &fin, 1);

    fprintf(stderr, "[file] Downloaded %s (%llu bytes)  OK\n",
            fname.c_str(), (unsigned long long)fsize);
    return true;
}

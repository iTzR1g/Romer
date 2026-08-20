#include "stream.h"
#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

// ─── Platform socket glue ─────────────────────────────────────
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SOCK_T = SOCKET;
    #define SOCK_CLOSE(s)        closesocket(s)
    #define SOCK_ERR_VAL         INVALID_SOCKET
    #define SOCK_ERRNO           WSAGetLastError()
    #define SOCK_AGAIN           WSAEWOULDBLOCK
    #define SOCK_CONNREFUSED     WSAECONNREFUSED
    static const int INVALID_FD = -1;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cerrno>
    #include <poll.h>
    using SOCK_T = int;
    #define SOCK_CLOSE(s)        ::close(s)
    #define SOCK_ERR_VAL         (-1)
    #define SOCK_ERRNO           errno
    #define SOCK_AGAIN           EAGAIN
    static constexpr int INVALID_FD = -1;
#endif

// ─── Helpers ──────────────────────────────────────────────────
static bool socketStartup(bool* owns_wsa) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
    *owns_wsa = true;
#endif
    (void)owns_wsa;
    return true;
}

static void socketCleanup(bool owns_wsa) {
#ifdef _WIN32
    if (owns_wsa) WSACleanup();
#endif
    (void)owns_wsa;
}

static bool setNonBlocking(SOCK_T fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Read exactly |len| bytes (polls until done or error).
static bool recvAll(SOCK_T fd, void* buf, size_t len) {
    auto* ptr = static_cast<uint8_t*>(buf);
    while (len > 0) {
        int n = recv(fd, reinterpret_cast<char*>(ptr), static_cast<int>(len), 0);
        if (n <= 0) {
            if (n == 0) return false;                     // clean close
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {    // should not happen
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
#else
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
#endif
            return false;
        }
        ptr  += n;
        len  -= static_cast<size_t>(n);
    }
    return true;
}

// Send exactly |len| bytes.
static bool sendAll(SOCK_T fd, const void* buf, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        int n = send(fd, reinterpret_cast<const char*>(ptr),
                     static_cast<int>(len), 0);
        if (n <= 0) return false;
        ptr += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

static void setTcpNoDelay(SOCK_T fd) {
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));
}

// ──────────────────────────────────────────────────────────────
//  StreamSender
// ──────────────────────────────────────────────────────────────
StreamSender::StreamSender()  = default;
StreamSender::~StreamSender() { close(); }

bool StreamSender::listen(int port) {
    if (!socketStartup(&owns_wsa_)) return false;

    listen_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        return false;
    if (::listen(listen_fd_, 1) < 0)
        return false;

    fprintf(stderr, "[server] Listening on 0.0.0.0:%d\n", port);
    return true;
}

bool StreamSender::acceptClient(int timeout_ms) {
    if (listen_fd_ < 0) return false;
    if (client_fd_ >= 0) return true;   // already connected

    // Use poll() / select() for timeout support.
#ifdef _WIN32
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listen_fd_, &readfds);
    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(0, &readfds, nullptr, nullptr,
                     timeout_ms >= 0 ? &tv : nullptr);
    if (ret <= 0) return false;
#else
    struct pollfd pfd{};
    pfd.fd     = listen_fd_;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) return false;
#endif

    sockaddr_in client_addr{};
    socklen_t   client_len = sizeof(client_addr);

    SOCK_T raw_fd = ::accept(listen_fd_,
                              reinterpret_cast<sockaddr*>(&client_addr),
                              &client_len);
    if (raw_fd == SOCK_ERR_VAL) return false;

    client_fd_ = static_cast<int>(raw_fd);
    setTcpNoDelay(client_fd_);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    fprintf(stderr, "[server] Client connected from %s\n", ip);
    return true;
}

bool StreamSender::sendFrame(int width, int height,
                              const uint8_t* jpeg_data, size_t jpeg_size) {
    if (client_fd_ < 0) return false;

    FrameHeader hdr{};
    hdr.width    = htonl(static_cast<uint32_t>(width));
    hdr.height   = htonl(static_cast<uint32_t>(height));
    hdr.jpeg_size = htonl(static_cast<uint32_t>(jpeg_size));

    return sendAll(client_fd_, &hdr, sizeof(hdr)) &&
           sendAll(client_fd_, jpeg_data, jpeg_size);
}

bool StreamSender::isConnected() const { return client_fd_ >= 0; }

void StreamSender::disconnect() {
    if (client_fd_ >= 0) {
        SOCK_CLOSE(client_fd_);
        client_fd_ = -1;
        fprintf(stderr, "[server] Client disconnected\n");
    }
}

void StreamSender::close() {
    disconnect();
    if (listen_fd_ >= 0) {
        SOCK_CLOSE(listen_fd_);
        listen_fd_ = -1;
    }
    socketCleanup(owns_wsa_);
    owns_wsa_ = false;
}

// ──────────────────────────────────────────────────────────────
//  StreamReceiver
// ──────────────────────────────────────────────────────────────
StreamReceiver::StreamReceiver()  = default;
StreamReceiver::~StreamReceiver() { disconnect(); }

bool StreamReceiver::connect(const std::string& host, int port) {
    if (!socketStartup(&owns_wsa_)) return false;

    sock_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (sock_fd_ < 0) return false;

    setTcpNoDelay(sock_fd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        fprintf(stderr, "[client] Invalid address: %s\n", host.c_str());
        return false;
    }

    if (::connect(sock_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "[client] Connection to %s:%d failed\n",
                host.c_str(), port);
        return false;
    }

    fprintf(stderr, "[client] Connected to %s:%d\n", host.c_str(), port);
    return true;
}

bool StreamReceiver::recvFrame(int& width, int& height,
                                std::vector<uint8_t>& jpeg_data) {
    if (sock_fd_ < 0) return false;

    FrameHeader hdr{};
    if (!recvAll(sock_fd_, &hdr, sizeof(hdr)))
        return false;

    width     = static_cast<int>(ntohl(hdr.width));
    height    = static_cast<int>(ntohl(hdr.height));
    uint32_t sz = ntohl(hdr.jpeg_size);

    jpeg_data.resize(sz);
    return recvAll(sock_fd_, jpeg_data.data(), sz);
}

void StreamReceiver::disconnect() {
    if (sock_fd_ >= 0) {
        SOCK_CLOSE(sock_fd_);
        sock_fd_ = -1;
    }
    socketCleanup(owns_wsa_);
    owns_wsa_ = false;
}

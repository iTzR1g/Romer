#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstddef>

// ─── StreamSender (server side) ───────────────────────────────
// Listens on a TCP port, accepts one client, then streams frames.
class StreamSender {
public:
    StreamSender();
    ~StreamSender();

    // Bind & start listening on |port|.
    bool listen(int port);

    // Block up to |timeout_ms| for an incoming client.
    // Returns true if a client connected.
    bool acceptClient(int timeout_ms = -1);

    // Send one frame. Returns false on disconnect / error.
    bool sendFrame(int width, int height, const uint8_t* jpeg_data, size_t jpeg_size);

    // True if a client is currently connected.
    bool isConnected() const;

    // Drop the current client (if any).
    void disconnect();

    // Close the listening socket too.
    void close();

private:
    int  listen_fd_ = -1;
    int  client_fd_ = -1;
    bool owns_wsa_  = false;   // Windows WSA startup flag

    bool setNonBlocking(int fd);
};

// ─── StreamReceiver (client side) ─────────────────────────────
class StreamReceiver {
public:
    StreamReceiver();
    ~StreamReceiver();

    // Connect to |host|:|port|.
    bool connect(const std::string& host, int port);

    // Receive one frame. Blocks until data arrives.
    // Returns false on disconnect / error.
    bool recvFrame(int& width, int& height, std::vector<uint8_t>& jpeg_data);

    // Close connection.
    void disconnect();

private:
    int  sock_fd_ = -1;
    bool owns_wsa_ = false;
};

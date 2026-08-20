#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

// ─── FileTransferServer ─────────────────────────────────────
// Listens on FILE_PORT (42818).  Handles one client at a time.
// Supports both push (client→server) and pull (server→client).
class FileTransferServer {
public:
    FileTransferServer()  = default;
    ~FileTransferServer();

    // Start listening.  Returns false on failure.
    bool listen(int port);

    // Accept a client and handle the complete transfer.
    // Blocks at most |timeout_ms| waiting for a client.
    void acceptAndHandle(int timeout_ms);

    // Shut down the listening socket.
    void close();

private:
    int  listen_fd_ = -1;
    bool owns_wsa_  = false;

    int  handleClient(int client_fd);
    int  handlePush(int client_fd, const std::string& filename, uint64_t size);
    int  handlePull(int client_fd, const std::string& filename);
};

// ─── FileTransferClient ─────────────────────────────────────
class FileTransferClient {
public:
    FileTransferClient()  = default;
    ~FileTransferClient();

    // Connect to server.
    bool connect(const std::string& host, int port);

    // Upload |local_path| to the server.
    bool sendFile(const std::string& local_path);

    // Download |remote_path| from the server, save as |local_path|.
    bool recvFile(const std::string& remote_path, const std::string& local_path);

    void disconnect();

private:
    int  sock_fd_  = -1;
    bool owns_wsa_ = false;
};

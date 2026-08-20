#pragma once

#include <string>
#include <cstdint>

// ─── RemoteShellServer ──────────────────────────────────────
// Listens on SHELL_PORT (42819).  Spawns a shell for each
// client and relays data between the TCP socket and the shell
// process (via a PTY on POSIX, pipes on Windows).
class RemoteShellServer {
public:
    RemoteShellServer()  = default;
    ~RemoteShellServer();

    bool listen(int port);

    // Accept one client, spawn a shell, relay I/O until
    // the client disconnects or the shell exits.
    void acceptAndHandle(int timeout_ms);

    void close();

private:
    int  listen_fd_ = -1;
    bool owns_wsa_  = false;

    // Platform-specific shell spawning and I/O relay.
    int  handleClient(int client_fd);
};

// ─── RemoteShellClient ──────────────────────────────────────
class RemoteShellClient {
public:
    RemoteShellClient()  = default;
    ~RemoteShellClient();

    bool connect(const std::string& host, int port);

    // Start an interactive shell session.
    // |shell_path| is sent to the server (e.g. "/bin/bash").
    // If empty the server picks a default.
    // On POSIX the local terminal is put in raw mode.
    void interactive(const std::string& shell_path = "");

    void disconnect();

private:
    int  sock_fd_  = -1;
    bool owns_wsa_ = false;
};

#pragma once

#include <cstdint>

// ─── Wire Protocol: Screen Frames ──────────────────────────
// TCP port 42817 – one-packet-per-frame layout:
//
//   [0..3]   width       (uint32, network byte order)
//   [4..7]   height      (uint32, network byte order)
//   [8..11]  jpeg_size   (uint32, network byte order)
//   [12..]   jpeg_data   (jpeg_size bytes of JPEG)

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
struct FrameHeader {
    uint32_t width;
    uint32_t height;
    uint32_t jpeg_size;
}
#ifndef _MSC_VER
__attribute__((packed))
#endif
;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

static constexpr int SCREEN_PORT  = 42817;   // screen mirror
static constexpr int FILE_PORT    = 42818;   // file transfer
static constexpr int SHELL_PORT   = 42819;   // remote shell

// ─── File Transfer protocol (port 42818) ─────────────────────
// Client → Server direction byte:
static constexpr uint8_t FILE_PUSH = 0;      // client uploads to server
static constexpr uint8_t FILE_PULL = 1;      // client downloads from server

// Maximum chunk payload (64 KB keeps TCP overhead low).
static constexpr size_t FILE_CHUNK_SIZE = 65536;

// ─── Remote Shell protocol (port 42819) ─────────────────────
// Stream identifiers inside SHELL_DATA:
static constexpr uint8_t SHELL_STDIN  = 0;
static constexpr uint8_t SHELL_STDOUT = 1;
static constexpr uint8_t SHELL_STDERR = 2;

// Max shell-path length the server will accept.
static constexpr size_t SHELL_PATH_MAX = 256;

#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

// CaptureBuffer holds a single screen-shot in plain RGB (3 bytes/pixel).
struct CaptureBuffer {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;   // size = width * height * 3
};

// Abstract interface each platform implements.
class ScreenCapture {
public:
    virtual ~ScreenCapture() = default;

    // Open the display / capture device.
    virtual bool init() = 0;

    // Grab the current screen contents into |buffer|.
    virtual bool capture(CaptureBuffer& buffer) = 0;

    // Release all resources.
    virtual void shutdown() = 0;

    // Human-readable name of the platform backend.
    virtual const char* backendName() const = 0;
};

// Factory – defined in the platform-specific .cpp / .mm file.
ScreenCapture* createScreenCapture();

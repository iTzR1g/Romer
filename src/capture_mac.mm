#include "capture.h"

#include <cstdio>

#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>

// ──────────────────────────────────────────────────────────────
//  macOS screen capture  –  CoreGraphics CGDisplay
// ──────────────────────────────────────────────────────────────
//
//  CGDisplayCreateImage() captures the whole main display.
//  We convert the resulting CGImage → RGBA → RGB.
//
class MacCapture : public ScreenCapture {
public:
    MacCapture()  = default;
    ~MacCapture() override { shutdown(); }

    bool init() override {
        display_id_ = CGMainDisplayID();
        width_  = static_cast<int>(CGDisplayPixelsWide(display_id_));
        height_ = static_cast<int>(CGDisplayPixelsHigh(display_id_));

        fprintf(stderr, "[capture] macOS CoreGraphics  |  %d x %d\n",
                width_, height_);
        return true;
    }

    bool capture(CaptureBuffer& buffer) override {
        CGImageRef image = CGDisplayCreateImage(display_id_);
        if (!image) return false;

        size_t w = CGImageGetWidth(image);
        size_t h = CGImageGetHeight(image);

        CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(image));
        if (!data) {
            CGImageRelease(image);
            return false;
        }

        const uint8_t* pixels = CFDataGetBytePtr(data);
        size_t bpr = CGImageGetBytesPerRow(image);

        buffer.width  = static_cast<int>(w);
        buffer.height = static_cast<int>(h);
        buffer.data.resize(w * h * 3);


        
        CGBitmapInfo info  = CGImageGetBitmapInfo(image);
        size_t       bpp   = CGImageGetBitsPerPixel(image) / 8;
        bool         bgra  = (info & kCGBitmapByteOrder32Little) != 0;

        for (size_t y = 0; y < h; ++y) {
            const uint8_t* row = pixels + y * bpr;
            for (size_t x = 0; x < w; ++x) {
                size_t off    = (y * w + x) * 3;
                const uint8_t* p = row + x * bpp;
                if (bgra) {
                    buffer.data[off + 0] = p[2];  // R ← B
                    buffer.data[off + 1] = p[1];  // G ← G
                    buffer.data[off + 2] = p[0];  // B ← R
                } else {
                    buffer.data[off + 0] = p[0];
                    buffer.data[off + 1] = p[1];
                    buffer.data[off + 2] = p[2];
                }
            }
        }

        CFRelease(data);
        CGImageRelease(image);
        return true;
    }

    void shutdown() override {}

    const char* backendName() const override {
        return "macOS CoreGraphics";
    }

private:
    CGDirectDisplayID display_id_ = 0;
    int width_  = 0;
    int height_ = 0;
};

ScreenCapture* createScreenCapture() {
    return new MacCapture();
}

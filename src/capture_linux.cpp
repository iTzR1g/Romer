#include "capture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

// ──────────────────────────────────────────────────────────────
//  X11 / XShm screen capture  –  the fastest path on Linux
// ──────────────────────────────────────────────────────────────
class X11Capture : public ScreenCapture {
public:
    X11Capture()  = default;
    ~X11Capture() override { shutdown(); }

    bool init() override {
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            fprintf(stderr, "[capture] Cannot open X display\n");
            return false;
        }

        screen_num_   = DefaultScreen(display_);
        root_         = RootWindow(display_, screen_num_);
        screen_width_  = DisplayWidth(display_, screen_num_);
        screen_height_ = DisplayHeight(display_, screen_num_);

        fprintf(stderr, "[capture] X11 backend  |  %d x %d  |  %s\n",
                screen_width_, screen_height_,
                DisplayString(display_));

        // Try MIT-SHM extension for zero-copy capture.
        int dummy;
        if (XShmQueryExtension(display_) &&
            XShmQueryVersion(display_, &dummy, &dummy, &dummy)) {
            image_ = XShmCreateImage(display_,
                                      DefaultVisual(display_, screen_num_),
                                      DefaultDepth(display_, screen_num_),
                                      ZPixmap, nullptr, &shm_info_,
                                      screen_width_, screen_height_);
            if (image_) {
                shm_info_.shmid = shmget(IPC_PRIVATE,
                                          image_->bytes_per_line * image_->height,
                                          IPC_CREAT | 0777);
                if (shm_info_.shmid >= 0) {
                    shm_info_.shmaddr = static_cast<char*>(
                        shmat(shm_info_.shmid, nullptr, 0));
                    if (shm_info_.shmaddr != reinterpret_cast<char*>(-1)) {
                        image_->data = shm_info_.shmaddr;
                        shm_info_.readOnly = False;
                        if (XShmAttach(display_, &shm_info_)) {
                            use_shm_ = true;
                        }
                    }
                }
            }
        }

        if (!use_shm_) {
            fprintf(stderr, "[capture] XShm unavailable — falling back to XGetImage (slower)\n");
        }
        return true;
    }

    bool capture(CaptureBuffer& buffer) override {
        int w = screen_width_;
        int h = screen_height_;

        if (use_shm_) {
            XShmGetImage(display_, root_, image_, 0, 0, AllPlanes);
        } else {
            if (image_) XDestroyImage(image_);
            image_ = XGetImage(display_, root_, 0, 0, w, h, AllPlanes, ZPixmap);
            if (!image_) return false;
        }

        buffer.width  = w;
        buffer.height = h;
        buffer.data.resize(w * h * 3);

        // Extract bit shifts from the visual's masks.
        int r_shift = ffs_mask(image_->red_mask);
        int g_shift = ffs_mask(image_->green_mask);
        int b_shift = ffs_mask(image_->blue_mask);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned long pixel = XGetPixel(image_, x, y);
                size_t idx = static_cast<size_t>(y * w + x) * 3;
                buffer.data[idx + 0] = (pixel >> r_shift) & 0xFF;
                buffer.data[idx + 1] = (pixel >> g_shift) & 0xFF;
                buffer.data[idx + 2] = (pixel >> b_shift) & 0xFF;
            }
        }

        return true;
    }

    void shutdown() override {
        if (use_shm_) {
            XShmDetach(display_, &shm_info_);
            shmdt(shm_info_.shmaddr);
            shmctl(shm_info_.shmid, IPC_RMID, nullptr);
            if (image_) {
                // The image data was the shared memory, so just free the struct.
                image_->data = nullptr;
                XDestroyImage(image_);
                image_ = nullptr;
            }
        } else if (image_) {
            XDestroyImage(image_);
            image_ = nullptr;
        }
        if (display_) {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }

    const char* backendName() const override {
        return use_shm_ ? "X11 + XShm" : "X11 (fallback)";
    }

private:
    Display* display_       = nullptr;
    Window   root_;
    int      screen_num_    = 0;
    int      screen_width_  = 0;
    int      screen_height_ = 0;
    XImage*  image_         = nullptr;
    XShmSegmentInfo shm_info_{};
    bool     use_shm_       = false;

    // Position of the first set bit in |mask| (0-based).
    static int ffs_mask(unsigned long mask) {
        if (mask == 0) return 0;
        int n = 0;
        while ((mask & 1) == 0) { mask >>= 1; ++n; }
        return n;
    }
};

// ─── Factory ──────────────────────────────────────────────────
ScreenCapture* createScreenCapture() {
    return new X11Capture();
}

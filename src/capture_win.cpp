#include "capture.h"

#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <comdef.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ──────────────────────────────────────────────────────────────
//  Windows DXGI Desktop Duplication – the fastest capture path
// ──────────────────────────────────────────────────────────────
//
//  IDXGIOutputDuplication gives us GPU-backed screen copies with
//  minimal CPU overhead.  Frames are only produced when the
//  desktop actually changes (we keep the last frame for repeats).
//
class WinCapture : public ScreenCapture {
public:
    WinCapture()  = default;
    ~WinCapture() override { shutdown(); }

    bool init() override {
        HRESULT hr;

        // 1. Create D3D11 device (hardware, no debug)
        UINT flags = 0;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
                                nullptr, flags, levels, 1,
                                D3D11_SDK_VERSION, &device_, nullptr, &ctx_);
        if (FAILED(hr)) {
            fprintf(stderr, "[capture] D3D11CreateDevice failed (0x%08lx)\n", hr);
            return false;
        }

        // 2. Get DXGI device → adapter → output
        IDXGIDevice*  dxgi_dev = nullptr;
        IDXGIAdapter* adapter  = nullptr;
        IDXGIOutput*  output   = nullptr;

        hr = device_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev);
        if (SUCCEEDED(hr)) hr = dxgi_dev->GetParent(__uuidof(IDXGIAdapter),
                                                     (void**)&adapter);
        if (SUCCEEDED(hr)) hr = adapter->EnumOutputs(0, &output);  // primary monitor
        if (FAILED(hr)) {
            fprintf(stderr, "[capture] Cannot enumerate outputs (0x%08lx)\n", hr);
            if (output)   output->Release();
            if (adapter)  adapter->Release();
            if (dxgi_dev) dxgi_dev->Release();
            return false;
        }

        // 3. Query output description for size
        DXGI_OUTPUT_DESC od{};
        output->GetDesc(&od);
        width_  = od.DesktopCoordinates.right  - od.DesktopCoordinates.left;
        height_ = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;

        fprintf(stderr, "[capture] DXGI backend  |  %d x %d\n", width_, height_);

        // 4. Create duplication interface
        IDXGIOutput1* output1 = nullptr;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
        if (SUCCEEDED(hr))
            hr = output1->DuplicateOutput(device_, &dup_);

        output1->Release();
        output->Release();
        adapter->Release();
        dxgi_dev->Release();

        if (FAILED(hr)) {
            fprintf(stderr, "[capture] DuplicateOutput failed (0x%08lx)\n", hr);
            return false;
        }

        // 5. Staging texture for CPU readback
        D3D11_TEXTURE2D_DESC td{};
        td.Width          = width_;
        td.Height         = height_;
        td.MipLevels      = 1;
        td.ArraySize      = 1;
        td.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage          = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.BindFlags      = 0;

        hr = device_->CreateTexture2D(&td, nullptr, &staging_);
        if (FAILED(hr)) {
            fprintf(stderr, "[capture] Create staging texture failed\n");
            return false;
        }

        // Allocate a persistent RGB buffer so we can repeat frames
        // when the desktop hasn't changed (AcquireNextFrame timeout).
        cached_.width  = width_;
        cached_.height = height_;
        cached_.data.resize(width_ * height_ * 3);

        return true;
    }

    bool capture(CaptureBuffer& buffer) override {
        HRESULT hr;

        // Try to acquire the next desktop frame (0 ms timeout).
        IDXGIResource* res = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info{};
        hr = dup_->AcquireNextFrame(0, &info, &res);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No desktop change – re-send cached frame.
            buffer = cached_;
            return true;
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Desktop mode changed (resolution, lock screen, etc.).
            // The caller should re-init, but we try once more.
            fprintf(stderr, "[capture] DXGI access lost – re-creating…\n");
            return false;
        }

        if (FAILED(hr)) {
            fprintf(stderr, "[capture] AcquireNextFrame error 0x%08lx\n", hr);
            return false;
        }

        // Get the texture from the resource.
        ID3D11Texture2D* acquired = nullptr;
        hr = res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquired);
        res->Release();

        if (FAILED(hr)) {
            dup_->ReleaseFrame();
            return false;
        }

        // Copy to staging for CPU read.
        ctx_->CopyResource(staging_, acquired);
        acquired->Release();

        // Map and convert BGRA → RGB.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = ctx_->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            buffer.width  = width_;
            buffer.height = height_;
            buffer.data.resize(width_ * height_ * 3);

            auto* src = static_cast<const uint8_t*>(mapped.pData);
            auto* dst = buffer.data.data();

            for (int y = 0; y < height_; ++y) {
                const uint8_t* row = src + y * mapped.RowPitch;
                for (int x = 0; x < width_; ++x) {
                    size_t si = x * 4;
                    size_t di = (y * width_ + x) * 3;
                    dst[di]     = row[si + 2];  // R
                    dst[di + 1] = row[si + 1];  // G
                    dst[di + 2] = row[si];      // B
                }
            }

            ctx_->Unmap(staging_, 0);

            // Cache for next timeout.
            cached_ = buffer;
        }

        dup_->ReleaseFrame();
        return SUCCEEDED(hr);
    }

    void shutdown() override {
        if (dup_)     { dup_->Release();      dup_     = nullptr; }
        if (staging_) { staging_->Release();   staging_ = nullptr; }
        if (ctx_)     { ctx_->Release();       ctx_     = nullptr; }
        if (device_)  { device_->Release();    device_  = nullptr; }
    }

    const char* backendName() const override { return "Windows DXGI"; }

private:
    ID3D11Device*           device_  = nullptr;
    ID3D11DeviceContext*    ctx_     = nullptr;
    IDXGIOutputDuplication* dup_     = nullptr;
    ID3D11Texture2D*        staging_ = nullptr;
    int                     width_   = 0;
    int                     height_  = 0;
    CaptureBuffer           cached_;   // last good frame (for repeats)
};

ScreenCapture* createScreenCapture() {
    return new WinCapture();
}

#include "jpeg_utils.h"

#include <cstdio>
#include <csetjmp>
#include <cstring>

// libjpeg headers – works with both libjpeg and libjpeg-turbo.
#include <jpeglib.h>

namespace jpeg {
namespace {

// Error manager so we don't kill the process on corrupt JPEGs.
struct JpegErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf jmp;
};

static void jpegErrorExit(j_common_ptr cinfo) {
    JpegErrorMgr* myerr = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->jmp, 1);
}

// ─── Destination manager: writes to an in-memory vector ──────
struct JpegDestMgr {
    jpeg_destination_mgr pub;
    std::vector<uint8_t>* dest;
    uint8_t buffer[4096];
};

static void initDest(j_compress_ptr) {}
static boolean emptyDest(j_compress_ptr cinfo) {
    JpegDestMgr* m = reinterpret_cast<JpegDestMgr*>(cinfo->dest);
    m->dest->insert(m->dest->end(), m->buffer, m->buffer + sizeof(m->buffer));
    m->pub.next_output_byte = m->buffer;
    m->pub.free_in_buffer = sizeof(m->buffer);
    return TRUE;
}
static void termDest(j_compress_ptr cinfo) {
    JpegDestMgr* m = reinterpret_cast<JpegDestMgr*>(cinfo->dest);
    size_t remaining = sizeof(m->buffer) - m->pub.free_in_buffer;
    if (remaining > 0)
        m->dest->insert(m->dest->end(), m->buffer, m->buffer + remaining);
}

// ─── Source manager: reads from an in-memory buffer ──────────
struct JpegSrcMgr {
    jpeg_source_mgr pub;
    const uint8_t* data;
    size_t size;
    uint8_t buffer[4096];
};

static void initSrc(j_decompress_ptr) {}
static boolean fillSrc(j_decompress_ptr cinfo) {
    JpegSrcMgr* m = reinterpret_cast<JpegSrcMgr*>(cinfo->src);
    m->pub.next_input_byte = m->data;
    m->pub.bytes_in_buffer = m->size;
    return TRUE;
}
static void skipSrc(j_decompress_ptr cinfo, long num_bytes) {
    JpegSrcMgr* m = reinterpret_cast<JpegSrcMgr*>(cinfo->src);
    if (num_bytes <= 0) return;
    if ((size_t)num_bytes > m->pub.bytes_in_buffer) {
        m->pub.next_input_byte = nullptr;
        m->pub.bytes_in_buffer = 0;
    } else {
        m->pub.next_input_byte += num_bytes;
        m->pub.bytes_in_buffer -= num_bytes;
    }
}
static void termSrc(j_decompress_ptr) {}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
bool encode(const uint8_t* rgb_data, int width, int height, int quality,
            std::vector<uint8_t>& output) {
    output.clear();

    jpeg_compress_struct cinfo{};
    JpegErrorMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;

    if (setjmp(jerr.jmp)) {
        jpeg_destroy_compress(&cinfo);
        return false;
    }

    jpeg_create_compress(&cinfo);

    JpegDestMgr dest{};
    dest.dest = &output;
    dest.pub.init_destination    = initDest;
    dest.pub.empty_output_buffer = emptyDest;
    dest.pub.term_destination    = termDest;
    dest.pub.next_output_byte    = dest.buffer;
    dest.pub.free_in_buffer      = sizeof(dest.buffer);
    cinfo.dest = &dest.pub;

    cinfo.image_width       = width;
    cinfo.image_height      = height;
    cinfo.input_components  = 3;
    cinfo.in_color_space    = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        const uint8_t* row = rgb_data + cinfo.next_scanline * width * 3;
        jpeg_write_scanlines(&cinfo, const_cast<uint8_t**>(&row), 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return true;
}

// ──────────────────────────────────────────────────────────────
bool decode(const uint8_t* jpeg_data, size_t jpeg_size,
            std::vector<uint8_t>& rgb_output, int& out_width, int& out_height) {
    jpeg_decompress_struct cinfo{};
    JpegErrorMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;

    if (setjmp(jerr.jmp)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);

    JpegSrcMgr src{};
    src.data = jpeg_data;
    src.size = jpeg_size;
    src.pub.init_source       = initSrc;
    src.pub.fill_input_buffer = fillSrc;
    src.pub.skip_input_data   = skipSrc;
    src.pub.resync_to_restart = jpeg_resync_to_restart;
    src.pub.term_source       = termSrc;
    src.pub.next_input_byte   = jpeg_data;
    src.pub.bytes_in_buffer   = jpeg_size;
    cinfo.src = &src.pub;

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    out_width  = cinfo.output_width;
    out_height = cinfo.output_height;
    rgb_output.resize(out_width * out_height * 3);

    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* row = rgb_output.data() + cinfo.output_scanline * out_width * 3;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

}  // namespace jpeg

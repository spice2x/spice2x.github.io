#include "jpeg_encoder.h"

#ifdef SPICE_JPEG

#include <csetjmp>
#include <cstdio>

#include <jpeglib.h>

namespace jpeg_encoder {

namespace {

constexpr size_t CHUNK_SIZE = 16 * 1024;

// libjpeg writes through a destination manager; this one appends straight into
// the caller's vector so the encoded frame is never copied
struct VectorDestination {
    jpeg_destination_mgr mgr {};
    std::vector<uint8_t> *out = nullptr;
    uint8_t chunk[CHUNK_SIZE] {};
};

void dest_init(j_compress_ptr cinfo) {
    auto dest = reinterpret_cast<VectorDestination *>(cinfo->dest);
    dest->mgr.next_output_byte = dest->chunk;
    dest->mgr.free_in_buffer = CHUNK_SIZE;
}

// libjpeg cannot unwind a C++ exception out of its own frames, so growing the
// output has to fail by value and be turned into an error_exit by the caller
bool dest_append(VectorDestination *dest, size_t size) {
    try {
        dest->out->insert(dest->out->end(), dest->chunk, dest->chunk + size);
        return true;
    } catch (...) {
        return false;
    }
}

boolean dest_empty(j_compress_ptr cinfo) {
    auto dest = reinterpret_cast<VectorDestination *>(cinfo->dest);

    // returning FALSE would mean suspension to libjpeg, not failure
    if (!dest_append(dest, CHUNK_SIZE)) {
        (*cinfo->err->error_exit)(reinterpret_cast<j_common_ptr>(cinfo));
    }

    dest->mgr.next_output_byte = dest->chunk;
    dest->mgr.free_in_buffer = CHUNK_SIZE;
    return TRUE;
}

void dest_term(j_compress_ptr cinfo) {
    auto dest = reinterpret_cast<VectorDestination *>(cinfo->dest);
    if (!dest_append(dest, CHUNK_SIZE - dest->mgr.free_in_buffer)) {
        (*cinfo->err->error_exit)(reinterpret_cast<j_common_ptr>(cinfo));
    }
}

// the default handler calls exit(), which is not an option inside a game process
struct ErrorManager {
    jpeg_error_mgr mgr {};
    jmp_buf escape {};
};

void on_error(j_common_ptr cinfo) {
    longjmp(reinterpret_cast<ErrorManager *>(cinfo->err)->escape, 1);
}

void on_message(j_common_ptr) {
}

} // namespace

bool encode(
        std::vector<uint8_t> &out,
        const uint8_t *pixels,
        int width,
        int height,
        int quality) {

    if (!pixels || width <= 0 || height <= 0) {
        return false;
    }

    if (quality < 1) {
        quality = 1;
    } else if (quality > 100) {
        quality = 100;
    }

    // zero init is load bearing: the trap below is armed before the struct is
    // created, and jpeg_destroy_compress only tolerates that on a zeroed struct
    jpeg_compress_struct cinfo {};
    ErrorManager err;
    VectorDestination dest;

    cinfo.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = on_error;
    err.mgr.output_message = on_message;

    if (setjmp(err.escape)) {
        jpeg_destroy_compress(&cinfo);
        return false;
    }

    jpeg_create_compress(&cinfo);

    dest.out = &out;
    dest.mgr.init_destination = dest_init;
    dest.mgr.empty_output_buffer = dest_empty;
    dest.mgr.term_destination = dest_term;
    cinfo.dest = &dest.mgr;

    cinfo.image_width = static_cast<JDIMENSION>(width);
    cinfo.image_height = static_cast<JDIMENSION>(height);
    cinfo.in_color_space = JCS_RGB;
    cinfo.input_components = 3;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    // 4:2:0, matching what the capture path asked the previous encoder for
    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;

    jpeg_start_compress(&cinfo, TRUE);

    const size_t pitch = static_cast<size_t>(width) * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        auto row = const_cast<uint8_t *>(pixels + cinfo.next_scanline * pitch);
        JSAMPROW rows[1] = { row };
        jpeg_write_scanlines(&cinfo, rows, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return true;
}
}

#else // SPICE_JPEG

namespace jpeg_encoder {

// builds without libjpeg-turbo (the WinXP toolchains) simply cannot encode;
// callers already treat a false return as "no frame available"
bool encode(std::vector<uint8_t> &out, const uint8_t *, int, int, int) {
    out.clear();
    return false;
}
}

#endif // SPICE_JPEG

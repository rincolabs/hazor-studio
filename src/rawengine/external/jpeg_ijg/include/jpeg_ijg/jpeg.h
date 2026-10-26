#pragma once

#include <csetjmp>
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#include <jpeglib.h>

extern GLOBAL(struct jpeg_error_mgr *)
my_jpeg_std_error (struct jpeg_error_mgr * err);

extern GLOBAL(void)
my_jpeg_stdio_src (j_decompress_ptr cinfo, FILE * infile);

GLOBAL(void)
jpeg_memory_src (j_decompress_ptr cinfo, const JOCTET * buffer, size_t bufsize);

/**
 * @brief jpeg from file and memory use this as base to managers
 */
typedef struct {
    struct jpeg_source_mgr pub; /* public fields */
    jmp_buf error_jmp_buf;      /* error handler for this instance */
} rt_jpeg_error_mgr;

/*
 * ---- Hazor local modification; see src/rawengine/PROVENANCE.md ----
 *
 * Both source managers built on rt_jpeg_error_mgr's layout embed a jmp_buf,
 * and on MinGW x86-64 jmp_buf is __m128-based: alignof(the struct) is 16
 * there, and GCC writes the manager's function-pointer fields with *aligned*
 * SSE stores (movaps) on the strength of that. libjpeg's own alloc_small()
 * promises no such thing — only ALIGN_SIZE, which is
 * MAX(sizeof(void *), sizeof(double)) == 8 for the libjpeg-turbo this links
 * (built without SIMD). A pool address that is merely 8-byte aligned then
 * makes those stores raise a general-protection fault, which Windows reports
 * as EXCEPTION_ACCESS_VIOLATION (0xc0000005) rather than as a misalignment.
 *
 * So over-allocate and round the address up: the object has to sit where its
 * type says it does. Linux never saw this because glibc's jmp_buf is only
 * 8-byte aligned, which leaves the structs at alignof 8 and GCC emitting
 * plain 8-byte stores.
 */
static inline void *
rt_jpeg_alloc_aligned (j_common_ptr cinfo, size_t size, size_t alignment)
{
    const uintptr_t raw = (uintptr_t) (*cinfo->mem->alloc_small)
                          (cinfo, JPOOL_PERMANENT, size + alignment - 1);
    return (void *) ((raw + alignment - 1) & ~(uintptr_t) (alignment - 1));
}

#ifdef __cplusplus
}
#endif

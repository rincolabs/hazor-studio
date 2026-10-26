# Build rules for the bundled third-party libraries rtengine depends on.
#
# Upstream RawTherapee builds these via external/CMakeLists.txt + an autotools
# shell-out for LibRaw (external/LibRaw.cmake: autoreconf/configure/make) —
# that does not run under MSVC. Replaced here with plain add_library() calls
# enumerating the exact source lists upstream's own build files compile. See
# ../PROVENANCE.md.

add_library(rawengine_cjson STATIC cJSON/src/cJSON.c)
target_include_directories(rawengine_cjson
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/include/cjson
)

add_library(rawengine_jpeg_ijg STATIC
    jpeg_ijg/src/jdatasrc.cc
    jpeg_ijg/src/jpeg_memsrc.cc
)
target_include_directories(rawengine_jpeg_ijg
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/jpeg_ijg/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/jpeg_ijg/include/jpeg_ijg
)
target_link_libraries(rawengine_jpeg_ijg PUBLIC Rawengine::JPEG)

add_library(rawengine_klt STATIC
    klt/src/convolve.cc
    klt/src/error.cc
    klt/src/klt_util.cc
    klt/src/klt.cc
    klt/src/pnmio.cc
    klt/src/pyramid.cc
    klt/src/selectGoodFeatures.cc
    klt/src/storeFeatures.cc
    klt/src/trackFeatures.cc
    klt/src/writeFeatures.cc
)
target_include_directories(rawengine_klt
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/klt/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/klt/include/klt
)

# LibRaw — source list mirrors libraw/Makefile.am's lib_libraw_r_a_SOURCES
# (the reentrant/thread-safe variant RawTherapee links; built WITHOUT
# -DLIBRAW_NOTHREADS, unlike the plain libraw.a variant upstream also offers).
# The three src/*/{postprocessing,preprocessing,write}_ph.cpp files exist in
# the tree but are not part of that source list either upstream or here.
add_library(rawengine_libraw STATIC
    libraw/src/libraw_c_api.cpp
    libraw/src/libraw_datastream.cpp
    libraw/src/decoders/canon_600.cpp
    libraw/src/decoders/crx.cpp
    libraw/src/decoders/pana8.cpp
    libraw/src/decoders/decoders_dcraw.cpp
    libraw/src/decoders/sonycc.cpp
    libraw/src/decompressors/losslessjpeg.cpp
    libraw/src/decoders/decoders_libraw_dcrdefs.cpp
    libraw/src/decoders/olympus14.cpp
    libraw/src/decoders/decoders_libraw.cpp
    libraw/src/decoders/dng.cpp
    libraw/src/decoders/fp_dng.cpp
    libraw/src/decoders/fuji_compressed.cpp
    libraw/src/decoders/generic.cpp
    libraw/src/decoders/kodak_decoders.cpp
    libraw/src/decoders/load_mfbacks.cpp
    libraw/src/decoders/smal.cpp
    libraw/src/decoders/unpack_thumb.cpp
    libraw/src/decoders/unpack.cpp
    libraw/src/demosaic/aahd_demosaic.cpp
    libraw/src/demosaic/ahd_demosaic.cpp
    libraw/src/demosaic/dcb_demosaic.cpp
    libraw/src/demosaic/dht_demosaic.cpp
    libraw/src/demosaic/misc_demosaic.cpp
    libraw/src/demosaic/xtrans_demosaic.cpp
    libraw/src/integration/dngsdk_glue.cpp
    libraw/src/integration/rawspeed_glue.cpp
    libraw/src/metadata/adobepano.cpp
    libraw/src/metadata/canon.cpp
    libraw/src/metadata/ciff.cpp
    libraw/src/metadata/cr3_parser.cpp
    libraw/src/metadata/epson.cpp
    libraw/src/metadata/exif_gps.cpp
    libraw/src/metadata/fuji.cpp
    libraw/src/metadata/identify_tools.cpp
    libraw/src/metadata/identify.cpp
    libraw/src/metadata/kodak.cpp
    libraw/src/metadata/leica.cpp
    libraw/src/metadata/makernotes.cpp
    libraw/src/metadata/mediumformat.cpp
    libraw/src/metadata/minolta.cpp
    libraw/src/metadata/misc_parsers.cpp
    libraw/src/metadata/nikon.cpp
    libraw/src/metadata/normalize_model.cpp
    libraw/src/metadata/olympus.cpp
    libraw/src/metadata/hasselblad_model.cpp
    libraw/src/metadata/p1.cpp
    libraw/src/metadata/pentax.cpp
    libraw/src/metadata/samsung.cpp
    libraw/src/metadata/sony.cpp
    libraw/src/metadata/tiff.cpp
    libraw/src/postprocessing/aspect_ratio.cpp
    libraw/src/postprocessing/dcraw_process.cpp
    libraw/src/postprocessing/mem_image.cpp
    libraw/src/postprocessing/postprocessing_aux.cpp
    libraw/src/postprocessing/postprocessing_utils_dcrdefs.cpp
    libraw/src/postprocessing/postprocessing_utils.cpp
    libraw/src/preprocessing/ext_preprocess.cpp
    libraw/src/preprocessing/raw2image.cpp
    libraw/src/preprocessing/subtract_black.cpp
    libraw/src/tables/cameralist.cpp
    libraw/src/tables/colorconst.cpp
    libraw/src/tables/colordata.cpp
    libraw/src/tables/wblists.cpp
    libraw/src/utils/curves.cpp
    libraw/src/utils/decoder_info.cpp
    libraw/src/utils/init_close_utils.cpp
    libraw/src/utils/open.cpp
    libraw/src/utils/phaseone_processing.cpp
    libraw/src/utils/read_utils.cpp
    libraw/src/utils/thumb_utils.cpp
    libraw/src/utils/utils_dcraw.cpp
    libraw/src/utils/utils_libraw.cpp
    libraw/src/write/apply_profile.cpp
    libraw/src/write/file_write.cpp
    libraw/src/write/tiff_writer.cpp
    libraw/src/x3f/x3f_parse_process.cpp
    libraw/src/x3f/x3f_utils_patched.cpp
)
target_include_directories(rawengine_libraw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libraw)
target_compile_definitions(rawengine_libraw PRIVATE LIBRAW_BUILDLIB CMS_NO_REGISTER_KEYWORD=1)
target_link_libraries(rawengine_libraw PUBLIC
    Rawengine::JPEG
    Rawengine::ZLIB
    LCMS2::LCMS2
)
if(UNIX)
    target_link_libraries(rawengine_libraw PUBLIC Threads::Threads)
    target_compile_options(rawengine_libraw PRIVATE -pthread)
endif()

# `rawengine` — static library wrapping RawTherapee's rtengine. See
# PROVENANCE.md for what was vendored and the current port status.
#
# Fase 1 + Fase 2 (tmp/feat-raw-therapee-plan.md) are done on Linux: this
# target builds and links cleanly, via the rtcompat/ compatibility layer
# below that replaces rtengine's glibmm/gtkmm/cairomm/rtgui includes.
# MSVC/Windows has not been attempted (no toolchain in this environment).

add_subdirectory(external)

add_library(rawengine STATIC
    # rtcompat/ — the Fase 2 compatibility layer (see PROVENANCE.md /
    # tmp/feat-raw-therapee-plan.md): everything rtengine used to reach via
    # glibmm/gtkmm/cairomm/rtgui now reaches via here instead.
    rtcompat/glib/ustring.cpp
    rtcompat/glib/textconv.cpp
    rtcompat/glib/fileio.cpp
    rtcompat/glib/keyfile.cpp
    rtcompat/rtgui/options.cpp
    rtcompat/rtgui/paramsedited.cc
    rtcompat/rtgui/editcallbacks.cpp

    rtengine/newdelete.cc
    rtengine/alpha.cc
    rtengine/ahd_demosaic_RT.cc
    rtengine/amaze_demosaic_RT.cc
    rtengine/aspectratios.cc
    rtengine/badpixels.cc
    rtengine/bayer_bilinear_demosaic.cc
    rtengine/boxblur.cc
    rtengine/canon_cr3_decoder.cc
    rtengine/CA_correct_RT.cc
    rtengine/calc_distort.cc
    rtengine/camconst.cc
    rtengine/capturesharpening.cc
    rtengine/cfa_linedn_RT.cc
    rtengine/ciecam02.cc
    rtengine/cieimage.cc
    rtengine/clutstore.cc
    rtengine/color.cc
    rtengine/colortemp.cc
    rtengine/coord.cc
    rtengine/cplx_wavelet_dec.cc
    rtengine/curves.cc
    rtengine/dcp.cc
    rtengine/dcraw.cc
    rtengine/dcrop.cc
    rtengine/demosaic_algos.cc
    rtengine/dfmanager.cc
    rtengine/diagonalcurves.cc
    rtengine/dirpyr_equalizer.cc
    rtengine/dual_demosaic_RT.cc
    rtengine/dynamicprofile.cc
    rtengine/eahd_demosaic.cc
    rtengine/EdgePreservingDecomposition.cc
    rtengine/fast_demo.cc
    rtengine/ffmanager.cc
    rtengine/filmnegativeproc.cc
    rtengine/flatcurves.cc
    rtengine/FTblockDN.cc
    rtengine/gamutwarning.cc
    rtengine/gauss.cc
    rtengine/green_equil_RT.cc
    rtengine/guidedfilter.cc
    rtengine/hilite_recon.cc
    rtengine/histmatching.cc
    rtengine/hphd_demosaic_RT.cc
    rtengine/iccjpeg.cc
    rtengine/iccstore.cc
    rtengine/iimage.cc
    rtengine/image16.cc
    rtengine/image8.cc
    rtengine/imagedata.cc
    rtengine/imagedimensions.cc
    rtengine/imagefloat.cc
    rtengine/imageio.cc
    rtengine/imagesource.cc
    rtengine/improccoordinator.cc
    rtengine/improcfun.cc
    rtengine/impulse_denoise.cc
    rtengine/init.cc
    rtengine/ipdehaze.cc
    rtengine/ipgrain.cc
    rtengine/iplab2rgb.cc
    rtengine/iplocallab.cc
    rtengine/iplabregions.cc
    rtengine/iplocalcontrast.cc
    rtengine/ipresize.cc
    rtengine/ipretinex.cc
    rtengine/ipshadowshighlights.cc
    rtengine/ipsharpen.cc
    rtengine/ipsharpenedges.cc
    rtengine/ipsoftlight.cc
    rtengine/iptoneequalizer.cc
    rtengine/iptransform.cc
    rtengine/ipvibrance.cc
    rtengine/ipwavelet.cc
    rtengine/labimage.cc
    rtengine/lcp.cc
    rtengine/lensmetadata.cc
    rtengine/lmmse_demosaic.cc
    rtengine/loadinitial.cc
    rtengine/metadata.cc
    rtengine/munselllch.cc
    rtengine/myfile.cc
    rtengine/panasonic_decoders.cc
    rtengine/pdaflinesfilter.cc
    rtengine/perspectivecorrection.cc
    rtengine/PF_correct_RT.cc
    rtengine/pipettebuffer.cc
    rtengine/pixelshift.cc
    rtengine/processingjob.cc
    rtengine/profilestore.cc
    rtengine/rawflatfield.cc
    rtengine/rawimage.cc
    rtengine/rawimagesource.cc
    rtengine/rcd_demosaic.cc
    rtengine/refreshmap.cc
    rtengine/rt_algo.cc
    rtengine/rtapp.cc
    rtengine/rtlensfun.cc
    rtengine/rtthumbnail.cc
    rtengine/shmap.cc
    rtengine/simpleprocess.cc
    rtengine/spot.cc
    rtengine/stdimagesource.cc
    rtengine/tmo_fattal02.cc
    rtengine/utils.cc
    rtengine/vng4_demosaic_RT.cc
    rtengine/xtrans_demosaic.cc
    rtengine/procparams.cc
    rtengine/params/advanced.cc
    rtengine/params/locallab.cc
    rtengine/params/raw.cc
    rtengine/params/serdes.cc
)

target_include_directories(rawengine
    PUBLIC
        # So the rest of Hazor (src/raw/, once it exists) can
        # #include "rtengine/x.h"
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        # So rtengine/*.cc's own #include "x.h" (no rtengine/ prefix) resolves.
        ${CMAKE_CURRENT_SOURCE_DIR}/rtengine
)

target_link_libraries(rawengine
    PUBLIC
        rawengine_libraw
        rawengine_cjson
        rawengine_jpeg_ijg
        rawengine_klt
        LCMS2::LCMS2
        FFTW3F::FFTW3F
        Exiv2::exiv2lib
        Rawengine::EXPAT
        Rawengine::PNG
        Rawengine::TIFF
        Rawengine::ZLIB
)

if(RAWENGINE_WITH_LENSFUN)
    target_link_libraries(rawengine PUBLIC Lensfun::Lensfun)
    target_compile_definitions(rawengine PUBLIC RAWENGINE_WITH_LENSFUN=1)

    if(RAWENGINE_LENSFUN_HAS_LOAD_DIRECTORY)
        # See the check in the root CMakeLists.txt: uses lfDatabase's own
        # directory scan instead of rtlensfun.cc's GLib-based fallback.
        set_source_files_properties(
            rtengine/rtlensfun.cc PROPERTIES
            COMPILE_DEFINITIONS RT_LENSFUN_HAS_LOAD_DIRECTORY
        )
    endif()
endif()

# tmp/feat-raw-therapee-plan.md Fase 2 item 8 — OpenMP policy.
#
# GCC/Clang (Linux, primary platform): full, unmodified OpenMP, same as
# upstream RawTherapee. Without this, every `#pragma omp` in rtengine is
# silently ignored and the engine runs single-threaded everywhere, not
# just on Windows.
#
# MSVC: deliberately **not** wired up yet (W1 milestone — see the plan).
# `reduction(max:/min:)` (71 sites / 20 files), `collapse(2)` and
# `#pragma omp simd` are OpenMP 3.0/3.1/4.0 constructs MSVC's own
# `/openmp` (2.0) does not support; passing it as-is would fail to build.
# W2 (RT_OMP(...) + reduction-clause rewrites at just those sites) is a
# separate, not-yet-done step — until then MSVC gets a correct, serial
# `rawengine` by simply never seeing `#pragma omp`.
if(NOT MSVC)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(rawengine PUBLIC OpenMP::OpenMP_CXX)
    else()
        message(WARNING "OpenMP not found: rawengine will build but run single-threaded.")
    endif()
else()
    # MSVC: silence C4068 (unknown pragma) for the `#pragma GCC ivdep` /
    # `#pragma GCC diagnostic` hints sprinkled through rtengine — they are
    # harmless on a compiler that doesn't understand them, but noisy.
    target_compile_options(rawengine PRIVATE /wd4068)
    # SSE2 is guaranteed on MSVC x86_64, but MSVC never defines the
    # __SSE2__ macro GCC/Clang use to gate rtengine's SSE2 code paths
    # (helpersse2.h and friends).
    target_compile_definitions(rawengine PRIVATE __SSE2__=1)
endif()

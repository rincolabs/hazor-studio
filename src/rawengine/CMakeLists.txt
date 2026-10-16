# `rawengine` — static library wrapping RawTherapee's rtengine. See
# PROVENANCE.md for what was vendored and the current port status.
#
# Fase 1 (tmp/feat-raw-therapee-plan.md): vendoring + build skeleton only. The
# rtcompat/ compatibility layer that replaces rtengine's glibmm/gtkmm/cairomm/
# rtgui includes is Fase 2 and does not exist yet, so this target is not
# expected to compile cleanly until that lands.

add_subdirectory(external)

add_library(rawengine STATIC
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
endif()

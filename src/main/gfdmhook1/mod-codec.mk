libs += gfdm-mod-codec
srcdir_gfdm-mod-codec := src/main/gfdmhook1
cppflags_gfdm-mod-codec := -I src/main/gfdmhook1/ifs/3rd_party/fmt/include -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DFMT_HEADER_ONLY
cxxflags_gfdm-mod-codec := -std=gnu++23 -O2 -Wall -ffunction-sections -fdata-sections
cppsrc_gfdm-mod-codec := mod-codec.cpp ifs/texbin.cpp ifs/3rd_party/lodepng.cpp \
    ifs/3rd_party/libsquish/alpha.cpp \
    ifs/3rd_party/libsquish/clusterfit.cpp \
    ifs/3rd_party/libsquish/colourblock.cpp \
    ifs/3rd_party/libsquish/colourfit.cpp \
    ifs/3rd_party/libsquish/colourset.cpp \
    ifs/3rd_party/libsquish/maths.cpp \
    ifs/3rd_party/libsquish/rangefit.cpp \
    ifs/3rd_party/libsquish/singlecolourfit.cpp \
    ifs/3rd_party/libsquish/squish.cpp

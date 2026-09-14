avsdlls += gfdmhook1

deplibs_gfdmhook1 := avs

ldflags_gfdmhook1 := \
    -liphlpapi \
    -luser32 \
    -lws2_32 \

libs_gfdmhook1 := \
    cconfig \
    geninput \
    hook \
    hooklib \
    iidxhook-util \
    p3io \
    p3ioemu \
    security \
    util \

src_gfdmhook1 := \
    config.c \
    dllmain.c \
    network.c \

dlls += gfdmhook1

deplibs_gfdmhook1 :=

ldflags_gfdmhook1 := \
    -liphlpapi \
    -luser32 \
    -lws2_32 \

libs_gfdmhook1 := \
    cconfig \
    hook \
    p3ioemu \
    security \
    util \

src_gfdmhook1 := \
    config.c \
    dllmain.c \
    network.c \

avsdlls += gfdmhook1

deplibs_gfdmhook1 := avs

ldflags_gfdmhook1 := \
    -ladvapi32 \
    -liphlpapi \
    -lopengl32 \
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
    avs-config.c \
    backup.c \
    bios.c \
    coin.c \
    codec.c \
    config.c \
    debug.c \
    display.c \
    effect.c \
    dllmain.c \
    guitar.c \
    lan.c \
    layered.c \
    logging.c \
    mplay.c \
    mods.c \
    network.c \
    power.c \
    profile.c \
    storage.c \

ifeq ($(GFDM_MOD),1)
libs_gfdmhook1 += gfdm-mod-codec
endif

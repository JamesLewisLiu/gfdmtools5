exes            += config
rc_config       := config.rc
cppflags_config := -DUNICODE

libs_config     := \
    eamio \
    geninput \
    hook \
    util \

ldflags_config  := \
    -lcomctl32 \
    -lcomdlg32 \
    -lgdi32 \
    -lsetupapi \
    -mwindows \

src_config      := \
    analogs.c \
    bind-adv.c \
    bind.c \
    bind-light.c \
    buttons.c \
    eam.c \
    gfdm-network.c \
    gfdm-network-config.c \
    hid-metadata.c \
    gametype.c \
    lights.c \
    main.c \
    schema.c \
    snap.c \
    spinner.c \
    usages.c \


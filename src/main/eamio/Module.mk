dlls            += eamio
libs_eamio      := geninput util
ldflags_eamio   := -lhid -lsetupapi
src_eamio       := \
    cardio.c \
    cardio-state.c \
    eam-api.c \
    eam-impl.c \
    eam-s11n.c \

testexes += geninput-naive-test

srcdir_geninput-naive-test := src
libs_geninput-naive-test := util
ldflags_geninput-naive-test := -lsetupapi

src_geninput-naive-test := \
    test/geninput/naive-test.c \
    main/config/snap.c \
    main/geninput/dev-list.c \
    main/geninput/hid.c \
    main/geninput/hid-mgr.c \
    main/geninput/kbd-data.c \
    main/geninput/mapper.c \
    main/geninput/mapper-s11n.c

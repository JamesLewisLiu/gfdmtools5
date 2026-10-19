testexes += geninput-naive-test

srcdir_geninput-naive-test := src/test/geninput
libs_geninput-naive-test := util
ldflags_geninput-naive-test := -lhid -lsetupapi

src_geninput-naive-test := \
    naive-core.c \
    naive-test.c

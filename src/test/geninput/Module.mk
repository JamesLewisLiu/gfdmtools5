testexes += geninput-naive-test

srcdir_geninput-naive-test := src/test/geninput
libs_geninput-naive-test := util
ldflags_geninput-naive-test := -lhid -lsetupapi -lwinmm

src_geninput-naive-test := \
    naive-core.c \
    naive-test.c


testexes += geninput-midi-test
srcdir_geninput-midi-test := src/test/geninput
src_geninput-midi-test := midi-test.c
libs_geninput-midi-test := util
ldflags_geninput-midi-test := -lhid -lsetupapi -lwinmm

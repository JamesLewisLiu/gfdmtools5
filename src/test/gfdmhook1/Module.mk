testexes += gfdm-profile-test

srcdir_gfdm-profile-test := src/test/gfdmhook1
src_gfdm-profile-test := profile-test.c
libs_gfdm-profile-test := security

testexes += gfdm-storage-test
srcdir_gfdm-storage-test := src/test/gfdmhook1
src_gfdm-storage-test := storage-test.c

testexes += gfdm-coin-test
srcdir_gfdm-coin-test := src/test/gfdmhook1
src_gfdm-coin-test := coin-test.c

testexes += gfdm-debug-test
srcdir_gfdm-debug-test := src/test/gfdmhook1
src_gfdm-debug-test := debug-test.c

testexes += gfdm-effect-test
srcdir_gfdm-effect-test := src/test/gfdmhook1
src_gfdm-effect-test := effect-test.c

testexes += gfdm-bootstrap-test
srcdir_gfdm-bootstrap-test := src/test/gfdmhook1
src_gfdm-bootstrap-test := bootstrap-test.c
libs_gfdm-bootstrap-test := hooklib hook security util

testexes += gfdm-config-test
srcdir_gfdm-config-test := src/test/gfdmhook1
src_gfdm-config-test := config-test.c

testexes += gfdm-layered-test
srcdir_gfdm-layered-test := src/test/gfdmhook1
src_gfdm-layered-test := layered-test.c
ldflags_gfdm-layered-test := -ladvapi32

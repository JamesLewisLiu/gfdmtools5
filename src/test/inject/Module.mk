testexes += gfdm-logging-test
srcdir_gfdm-logging-test := src/test/inject
src_gfdm-logging-test := logging-test.c
libs_gfdm-logging-test := util

# Private integration fixtures, never included in game packages.
exes += gfdm-log-game
srcdir_gfdm-log-game := src/test/inject
src_gfdm-log-game := log-game.c
ldflags_gfdm-log-game := -mwindows

testdlls += gfdm-log-hook
srcdir_gfdm-log-hook := src/test/inject
src_gfdm-log-hook := log-hook.c
libs_gfdm-log-hook := util

tests: $(bindir)/indep-32/gfdm-log-game.exe $(bindir)/indep-32/gfdm-log-hook.dll $(bindir)/indep-32/inject.exe

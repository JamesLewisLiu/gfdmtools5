# GFDM-only build graph. Required shared modules retain their upstream paths.
cflags += -DWIN32_LEAN_AND_MEAN -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -DCOBJMACROS -Wno-attributes
avsvers_32 := 1002
imps += avs

include src/main/cconfig/Module.mk
include src/main/config/Module.mk
include src/main/eamio/Module.mk
include src/main/geninput/Module.mk
include src/main/gfdmhook1/Module.mk
include src/main/hook/Module.mk
include src/main/hooklib/Module.mk
include src/main/iidxhook-util/Module.mk
include src/main/inject/Module.mk
include src/main/p3io/Module.mk
include src/main/p3ioemu/Module.mk
include src/main/security/Module.mk
include src/main/util/Module.mk

include src/test/geninput/Module.mk
include src/test/gfdmhook1/Module.mk

zipdir := $(BUILDDIR)/zip
$(zipdir)/:
	$(V)mkdir -p $@

$(zipdir)/gfdm-v%.zip: \
	build/bin/avs2_1002-32/gfdmhook1.dll \
		build/bin/indep-32/config.exe \
		build/bin/indep-32/eamio.dll \
		build/bin/indep-32/geninput.dll \
		build/bin/indep-32/inject.exe \
		dist/gfdm/config-gf.bat \
		dist/gfdm/config-dm.bat \
		dist/gfdm/gamestart-v%-gf.bat \
		dist/gfdm/gamestart-v%-dm.bat \
		dist/gfdm/gfdm-v%-gf.conf \
		dist/gfdm/gfdm-v%-dm.conf \
		dist/gfdm/assets/v%/CLVSD.ax \
		| $(zipdir)/
	$(V)echo ... $@
	$(V)rm -f $@
	$(V)zip -j $@ $^

gfdm_packages := $(foreach v,4 5 6 7 8,$(zipdir)/gfdm-v$(v).zip)
gfdm_tests := $(testexes:%=$(bindir)/indep-32/%.exe)
.PHONY: packages tests
packages: $(gfdm_packages)
tests: $(gfdm_tests)
all: packages tests

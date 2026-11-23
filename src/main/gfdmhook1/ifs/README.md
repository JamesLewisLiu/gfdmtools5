# ifs_layeredfs texbin codec subset

Source: https://github.com/mon/ifs_layeredfs
Revision: a0312f8a23a2a7823bdb6a0f634cb58847874c1b

This directory contains the upstream GFDM texbin codec and its required
headers, lodepng, libsquish, fmt and RapidXML. The upstream injector, archive
layer and AVS boot code are not integrated. The host adapter lives outside
this directory in mod-codec.cpp and mods.c.

Files are normalized to LF without trailing whitespace. texbin.cpp omits its unused avs.hpp include so
the standalone codec compiles as C++23; one fmt/chrono.h example comment is
translated to ASCII. No codec algorithms are changed. Modified files carry
notices; vendor-manifest.json records original and integrated SHA256 values.

The root repository Unlicense does not override these component licenses.
See LICENSE and dist/gfdm/IFS-MOD-LICENSES.txt for redistribution notices and
license texts. Keep that text with distributed binaries.

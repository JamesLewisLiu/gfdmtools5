#!/bin/sh
set -eu
for test in geninput-naive geninput-midi gfdm-profile gfdm-storage gfdm-coin gfdm-debug gfdm-effect gfdm-backup gfdm-config gfdm-display gfdm-logging; do
    wine "build/bin/indep-32/$test-test.exe"
done

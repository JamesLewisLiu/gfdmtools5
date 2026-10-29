#!/bin/sh
set -eu
for test in geninput-naive gfdm-profile gfdm-storage gfdm-coin gfdm-debug gfdm-effect gfdm-backup gfdm-config gfdm-layered gfdm-avs-config gfdm-display gfdm-logging gfdm-guitar; do
    wine "build/bin/indep-32/$test-test.exe"
done

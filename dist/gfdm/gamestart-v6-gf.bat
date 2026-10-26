@echo off

cd /d "%~dp0"
if not exist DEV mkdir DEV
if not exist DEV\GF mkdir DEV\GF

inject gfdmhook1.dll gdv6.exe -g --config gfdm-v6-gf.conf %*

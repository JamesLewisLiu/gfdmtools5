@echo off

cd /d "%~dp0"
if not exist DEV mkdir DEV
if not exist DEV\GF mkdir DEV\GF

inject gfdmhook1.dll gdv8.exe -g --config gfdm-v8-gf.conf %*

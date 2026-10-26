@echo off

cd /d "%~dp0"
if not exist DEV mkdir DEV
if not exist DEV\DM mkdir DEV\DM

inject gfdmhook1.dll gdv8.exe -d --config gfdm-v8-dm.conf %*

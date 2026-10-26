@echo off

cd /d "%~dp0"
if not exist DEV mkdir DEV
if not exist DEV\DM mkdir DEV\DM

inject gfdmhook1.dll gdv5.exe -d --config gfdm-v5-dm.conf %*

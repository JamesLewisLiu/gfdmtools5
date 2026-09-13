@echo off

cd /d %~dp0
if not exist dev mkdir dev
if not exist dev\nvram mkdir dev\nvram
if not exist dev\raw mkdir dev\raw

inject gfdmhook1.dll gdv4.exe -d -b gfdm-v4-boot.xml --config gfdm-v4-dm.conf %*

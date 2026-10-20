@echo off

cd /d %~dp0
if not exist dev mkdir dev
if not exist dev\nvram mkdir dev\nvram
if not exist dev\raw mkdir dev\raw

inject gfdmhook1.dll gdv8.exe -d -I gfdm-v8-ea3-ident-dm.xml -b gfdm-v8-boot.xml --config gfdm-v8-dm.conf %*

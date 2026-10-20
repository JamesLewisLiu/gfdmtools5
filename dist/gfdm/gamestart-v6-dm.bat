@echo off

cd /d %~dp0
if not exist dev mkdir dev
if not exist dev\nvram mkdir dev\nvram
if not exist dev\raw mkdir dev\raw

inject gfdmhook1.dll gdv6.exe -d -I gfdm-v6-ea3-ident-dm.xml -b gfdm-v6-boot.xml --config gfdm-v6-dm.conf %*

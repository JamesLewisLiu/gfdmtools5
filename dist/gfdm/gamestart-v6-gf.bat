@echo off

cd /d %~dp0
if not exist dev mkdir dev
if not exist dev\nvram mkdir dev\nvram
if not exist dev\raw mkdir dev\raw

inject gfdmhook1.dll gdv6.exe -g -I gfdm-ea3-ident.xml -b gfdm-boot.xml --config gfdm-v6-gf.conf %*

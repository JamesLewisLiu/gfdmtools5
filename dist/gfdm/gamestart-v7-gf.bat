@echo off

cd /d %~dp0
if not exist dev mkdir dev
if not exist dev\nvram mkdir dev\nvram
if not exist dev\raw mkdir dev\raw

inject gfdmhook1.dll gdv7.exe -g -I gfdm-v7-ea3-ident-gf.xml -b gfdm-v7-boot.xml --config gfdm-v7-gf.conf %*

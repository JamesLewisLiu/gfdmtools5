@echo off

cd /d %~dp0
if not exist CONF mkdir CONF
if not exist CONF\NVRAM mkdir CONF\NVRAM
if not exist CONF\RAW mkdir CONF\RAW

inject gfdmhook1.dll gdv4.exe -g --config gfdm-v4-gf.conf %*

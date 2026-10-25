@echo off
rem BMS 上位机启动脚本（双击运行；需已安装 Python 3.12 + pyserial）
cd /d "%~dp0"
"C:\Users\C\AppData\Local\Programs\Python\Python312\python.exe" host_gui.py
if errorlevel 1 pause

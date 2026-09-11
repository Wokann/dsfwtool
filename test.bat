@echo off
rem Each command below is independent; no variables, loops, or subroutines are used.
rem It exports plaintext, then compresses/encrypts explicitly during image creation.
rem Run this file from the repository root in Command Prompt or PowerShell.
rem Official cases compare exported headers and decrypted/decompressed components.
rem They do not claim whole-image identity because per-unit settings/calibration areas are not exported.

rem Every command below is intentionally independent and written on one physical line.


make dsfwtool || exit /b 1
.\release\dsfwtool.exe --version || exit /b 1

rem Official v1: inspect, export plaintext, recompress/re-encrypt, then compare header and plaintext components.
.\release\dsfwtool.exe -i .\firmware\v1.bin -o .temp\official\v1\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v1.bin -h .temp\official\v1\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v1\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v1\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v1\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v1\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v1\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v1\repack.bin -h .temp\official\v1\unpack\header.bin -p1 -comp -encrypt .temp\official\v1\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v1\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v1\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v1\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v1\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v1\repack.bin -h .temp\official\v1\repack\header.bin -p1 -decrypt -uncomp .temp\official\v1\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v1\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v1\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v1\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v1\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v1\unpack\header.bin" ".temp\official\v1\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v1\unpack\arm9_boot_code.bin" ".temp\official\v1\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v1\unpack\arm7_boot_code.bin" ".temp\official\v1\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v1\unpack\arm9_gui_code.bin" ".temp\official\v1\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v1\unpack\arm7_wifi_code.bin" ".temp\official\v1\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v1\unpack\data_gfx.bin" ".temp\official\v1\repack\data_gfx.bin" >nul || exit /b 1

rem Official v2.
.\release\dsfwtool.exe -i .\firmware\v2.bin -o .temp\official\v2\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v2.bin -h .temp\official\v2\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v2\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v2\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v2\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v2\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v2\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v2\repack.bin -h .temp\official\v2\unpack\header.bin -p1 -comp -encrypt .temp\official\v2\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v2\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v2\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v2\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v2\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v2\repack.bin -h .temp\official\v2\repack\header.bin -p1 -decrypt -uncomp .temp\official\v2\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v2\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v2\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v2\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v2\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v2\unpack\header.bin" ".temp\official\v2\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v2\unpack\arm9_boot_code.bin" ".temp\official\v2\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v2\unpack\arm7_boot_code.bin" ".temp\official\v2\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v2\unpack\arm9_gui_code.bin" ".temp\official\v2\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v2\unpack\arm7_wifi_code.bin" ".temp\official\v2\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v2\unpack\data_gfx.bin" ".temp\official\v2\repack\data_gfx.bin" >nul || exit /b 1

rem Official v3.
.\release\dsfwtool.exe -i .\firmware\v3.bin -o .temp\official\v3\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v3.bin -h .temp\official\v3\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v3\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v3\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v3\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v3\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v3\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v3\repack.bin -h .temp\official\v3\unpack\header.bin -p1 -comp -encrypt .temp\official\v3\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v3\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v3\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v3\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v3\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v3\repack.bin -h .temp\official\v3\repack\header.bin -p1 -decrypt -uncomp .temp\official\v3\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v3\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v3\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v3\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v3\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v3\unpack\header.bin" ".temp\official\v3\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v3\unpack\arm9_boot_code.bin" ".temp\official\v3\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v3\unpack\arm7_boot_code.bin" ".temp\official\v3\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v3\unpack\arm9_gui_code.bin" ".temp\official\v3\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v3\unpack\arm7_wifi_code.bin" ".temp\official\v3\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v3\unpack\data_gfx.bin" ".temp\official\v3\repack\data_gfx.bin" >nul || exit /b 1

rem Official v4.
.\release\dsfwtool.exe -i .\firmware\v4.bin -o .temp\official\v4\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v4.bin -h .temp\official\v4\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v4\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v4\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v4\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v4\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v4\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v4\repack.bin -h .temp\official\v4\unpack\header.bin -p1 -comp -encrypt .temp\official\v4\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v4\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v4\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v4\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v4\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v4\repack.bin -h .temp\official\v4\repack\header.bin -p1 -decrypt -uncomp .temp\official\v4\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v4\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v4\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v4\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v4\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v4\unpack\header.bin" ".temp\official\v4\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v4\unpack\arm9_boot_code.bin" ".temp\official\v4\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v4\unpack\arm7_boot_code.bin" ".temp\official\v4\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v4\unpack\arm9_gui_code.bin" ".temp\official\v4\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v4\unpack\arm7_wifi_code.bin" ".temp\official\v4\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v4\unpack\data_gfx.bin" ".temp\official\v4\repack\data_gfx.bin" >nul || exit /b 1

rem Official v5.
.\release\dsfwtool.exe -i .\firmware\v5.bin -o .temp\official\v5\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v5.bin -h .temp\official\v5\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v5\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v5\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v5\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v5\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v5\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v5\repack.bin -h .temp\official\v5\unpack\header.bin -p1 -comp -encrypt .temp\official\v5\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v5\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v5\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v5\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v5\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v5\repack.bin -h .temp\official\v5\repack\header.bin -p1 -decrypt -uncomp .temp\official\v5\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v5\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v5\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v5\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v5\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v5\unpack\header.bin" ".temp\official\v5\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v5\unpack\arm9_boot_code.bin" ".temp\official\v5\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v5\unpack\arm7_boot_code.bin" ".temp\official\v5\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v5\unpack\arm9_gui_code.bin" ".temp\official\v5\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v5\unpack\arm7_wifi_code.bin" ".temp\official\v5\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v5\unpack\data_gfx.bin" ".temp\official\v5\repack\data_gfx.bin" >nul || exit /b 1

rem Official v6.
.\release\dsfwtool.exe -i .\firmware\v6.bin -o .temp\official\v6\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v6.bin -h .temp\official\v6\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v6\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v6\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v6\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v6\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v6\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v6\repack.bin -h .temp\official\v6\unpack\header.bin -p1 -comp -encrypt .temp\official\v6\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v6\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v6\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v6\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v6\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v6\repack.bin -h .temp\official\v6\repack\header.bin -p1 -decrypt -uncomp .temp\official\v6\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v6\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v6\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v6\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v6\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v6\unpack\header.bin" ".temp\official\v6\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v6\unpack\arm9_boot_code.bin" ".temp\official\v6\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v6\unpack\arm7_boot_code.bin" ".temp\official\v6\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v6\unpack\arm9_gui_code.bin" ".temp\official\v6\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v6\unpack\arm7_wifi_code.bin" ".temp\official\v6\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v6\unpack\data_gfx.bin" ".temp\official\v6\repack\data_gfx.bin" >nul || exit /b 1

rem Official v7.
.\release\dsfwtool.exe -i .\firmware\v7.bin -o .temp\official\v7\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\v7.bin -h .temp\official\v7\unpack\header.bin -p1 -decrypt -uncomp .temp\official\v7\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v7\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\v7\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\v7\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v7\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\v7\repack.bin -h .temp\official\v7\unpack\header.bin -p1 -comp -encrypt .temp\official\v7\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\v7\unpack\arm7_boot_code.bin -p3 -comp .temp\official\v7\unpack\arm9_gui_code.bin -p4 -comp .temp\official\v7\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\v7\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\v7\repack.bin -h .temp\official\v7\repack\header.bin -p1 -decrypt -uncomp .temp\official\v7\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\v7\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\v7\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\v7\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\v7\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\v7\unpack\header.bin" ".temp\official\v7\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\v7\unpack\arm9_boot_code.bin" ".temp\official\v7\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v7\unpack\arm7_boot_code.bin" ".temp\official\v7\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\v7\unpack\arm9_gui_code.bin" ".temp\official\v7\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\v7\unpack\arm7_wifi_code.bin" ".temp\official\v7\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\v7\unpack\data_gfx.bin" ".temp\official\v7\repack\data_gfx.bin" >nul || exit /b 1

rem Official iQue v1.
.\release\dsfwtool.exe -i .\firmware\iquev1.bin -o .temp\official\iquev1\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\iquev1.bin -h .temp\official\iquev1\unpack\header.bin -p1 -decrypt -uncomp .temp\official\iquev1\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\iquev1\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\iquev1\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\iquev1\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\iquev1\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\iquev1\repack.bin -h .temp\official\iquev1\unpack\header.bin -p1 -comp -encrypt .temp\official\iquev1\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\iquev1\unpack\arm7_boot_code.bin -p3 -comp .temp\official\iquev1\unpack\arm9_gui_code.bin -p4 -comp .temp\official\iquev1\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\iquev1\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\iquev1\repack.bin -h .temp\official\iquev1\repack\header.bin -p1 -decrypt -uncomp .temp\official\iquev1\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\iquev1\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\iquev1\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\iquev1\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\iquev1\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\iquev1\unpack\header.bin" ".temp\official\iquev1\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\iquev1\unpack\arm9_boot_code.bin" ".temp\official\iquev1\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev1\unpack\arm7_boot_code.bin" ".temp\official\iquev1\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev1\unpack\arm9_gui_code.bin" ".temp\official\iquev1\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev1\unpack\arm7_wifi_code.bin" ".temp\official\iquev1\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev1\unpack\data_gfx.bin" ".temp\official\iquev1\repack\data_gfx.bin" >nul || exit /b 1

rem Official iQue v2.
.\release\dsfwtool.exe -i .\firmware\iquev2.bin -o .temp\official\iquev2\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\iquev2.bin -h .temp\official\iquev2\unpack\header.bin -p1 -decrypt -uncomp .temp\official\iquev2\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\iquev2\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\iquev2\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\iquev2\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\iquev2\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\iquev2\repack.bin -h .temp\official\iquev2\unpack\header.bin -p1 -comp -encrypt .temp\official\iquev2\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\iquev2\unpack\arm7_boot_code.bin -p3 -comp .temp\official\iquev2\unpack\arm9_gui_code.bin -p4 -comp .temp\official\iquev2\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\iquev2\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\iquev2\repack.bin -h .temp\official\iquev2\repack\header.bin -p1 -decrypt -uncomp .temp\official\iquev2\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\iquev2\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\iquev2\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\iquev2\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\iquev2\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\iquev2\unpack\header.bin" ".temp\official\iquev2\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\iquev2\unpack\arm9_boot_code.bin" ".temp\official\iquev2\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev2\unpack\arm7_boot_code.bin" ".temp\official\iquev2\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev2\unpack\arm9_gui_code.bin" ".temp\official\iquev2\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev2\unpack\arm7_wifi_code.bin" ".temp\official\iquev2\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\iquev2\unpack\data_gfx.bin" ".temp\official\iquev2\repack\data_gfx.bin" >nul || exit /b 1

rem Official Korean v1.
.\release\dsfwtool.exe -i .\firmware\korv1.bin -o .temp\official\korv1\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\korv1.bin -h .temp\official\korv1\unpack\header.bin -p1 -decrypt -uncomp .temp\official\korv1\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\korv1\unpack\arm7_boot_code.bin -p3 -uncomp .temp\official\korv1\unpack\arm9_gui_code.bin -p4 -uncomp .temp\official\korv1\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\official\korv1\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\official\korv1\repack.bin -h .temp\official\korv1\unpack\header.bin -p1 -comp -encrypt .temp\official\korv1\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\official\korv1\unpack\arm7_boot_code.bin -p3 -comp .temp\official\korv1\unpack\arm9_gui_code.bin -p4 -comp .temp\official\korv1\unpack\arm7_wifi_code.bin -p5 -comp .temp\official\korv1\unpack\data_gfx.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\official\korv1\repack.bin -h .temp\official\korv1\repack\header.bin -p1 -decrypt -uncomp .temp\official\korv1\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\official\korv1\repack\arm7_boot_code.bin -p3 -uncomp .temp\official\korv1\repack\arm9_gui_code.bin -p4 -uncomp .temp\official\korv1\repack\arm7_wifi_code.bin -p5 -uncomp .temp\official\korv1\repack\data_gfx.bin || exit /b 1
fc /b ".temp\official\korv1\unpack\header.bin" ".temp\official\korv1\repack\header.bin" >nul || exit /b 1
fc /b ".temp\official\korv1\unpack\arm9_boot_code.bin" ".temp\official\korv1\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\korv1\unpack\arm7_boot_code.bin" ".temp\official\korv1\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\official\korv1\unpack\arm9_gui_code.bin" ".temp\official\korv1\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\official\korv1\unpack\arm7_wifi_code.bin" ".temp\official\korv1\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\official\korv1\unpack\data_gfx.bin" ".temp\official\korv1\repack\data_gfx.bin" >nul || exit /b 1

rem Independent chained operations on official v7 use the exported plaintext.
.\release\dsfwtool.exe -p1 -comp -crypt .temp\official\v7\unpack\arm9_boot_code.bin -h .temp\official\v7\unpack\header.bin -o .temp\official\v7\chain\arm9_boot_code.encrypted || exit /b 1
.\release\dsfwtool.exe -p1 -decrypt -uncomp .temp\official\v7\chain\arm9_boot_code.encrypted -h .temp\official\v7\unpack\header.bin -o .temp\official\v7\chain\arm9_boot_code.redecoded || exit /b 1
fc /b ".temp\official\v7\unpack\arm9_boot_code.bin" ".temp\official\v7\chain\arm9_boot_code.redecoded" >nul || exit /b 1
.\release\dsfwtool.exe -p2 -comp -crypt .temp\official\v7\unpack\arm7_boot_code.bin -h .temp\official\v7\unpack\header.bin -o .temp\official\v7\chain\arm7_boot_code.encrypted || exit /b 1
.\release\dsfwtool.exe -p2 -decrypt -uncomp .temp\official\v7\chain\arm7_boot_code.encrypted -h .temp\official\v7\unpack\header.bin -o .temp\official\v7\chain\arm7_boot_code.redecoded || exit /b 1
fc /b ".temp\official\v7\unpack\arm7_boot_code.bin" ".temp\official\v7\chain\arm7_boot_code.redecoded" >nul || exit /b 1
.\release\dsfwtool.exe -p3 -comp .temp\official\v7\unpack\arm9_gui_code.bin -o .temp\official\v7\chain\arm9_gui_code.recompressed.p345 || exit /b 1
.\release\dsfwtool.exe -p3 -uncomp .temp\official\v7\chain\arm9_gui_code.recompressed.p345 -o .temp\official\v7\chain\arm9_gui_code.redecoded || exit /b 1
fc /b ".temp\official\v7\unpack\arm9_gui_code.bin" ".temp\official\v7\chain\arm9_gui_code.redecoded" >nul || exit /b 1

rem FlashMe v8a no-auto (short physical 0x...FE00 dump): plaintext extraction, recompression, and plaintext comparison.
.\release\dsfwtool.exe -i .\firmware\flashmev8a_noauto_tv.bin -o .temp\flashme\flashmev8a_noauto_tv\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\flashmev8a_noauto_tv.bin -h .temp\flashme\flashmev8a_noauto_tv\unpack\header.bin -fh .temp\flashme\flashmev8a_noauto_tv\unpack\header_flashme.bin -p1 -decrypt -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\arm7_boot_code.bin -p3 -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\arm9_gui_code.bin -p4 -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\data_gfx.bin -fp1 -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\arm9_boot_code_flashme.bin -fp2 -uncomp .temp\flashme\flashmev8a_noauto_tv\unpack\arm7_boot_code_flashme.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\flashme\flashmev8a_noauto_tv\repack.bin -h .temp\flashme\flashmev8a_noauto_tv\unpack\header.bin -fh .temp\flashme\flashmev8a_noauto_tv\unpack\header_flashme.bin -p1 -comp -encrypt .temp\flashme\flashmev8a_noauto_tv\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\flashme\flashmev8a_noauto_tv\unpack\arm7_boot_code.bin -p3 -comp -flashme .temp\flashme\flashmev8a_noauto_tv\unpack\arm9_gui_code.bin -p4 -comp -flashme .temp\flashme\flashmev8a_noauto_tv\unpack\arm7_wifi_code.bin -p5 -comp -flashme .temp\flashme\flashmev8a_noauto_tv\unpack\data_gfx.bin -fp1 -comp .temp\flashme\flashmev8a_noauto_tv\unpack\arm9_boot_code_flashme.bin -fp2 -comp .temp\flashme\flashmev8a_noauto_tv\unpack\arm7_boot_code_flashme.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\flashme\flashmev8a_noauto_tv\repack.bin -h .temp\flashme\flashmev8a_noauto_tv\repack\header.bin -fh .temp\flashme\flashmev8a_noauto_tv\repack\header_flashme.bin -p1 -decrypt -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\arm7_boot_code.bin -p3 -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\arm9_gui_code.bin -p4 -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\arm7_wifi_code.bin -p5 -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\data_gfx.bin -fp1 -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\arm9_boot_code_flashme.bin -fp2 -uncomp .temp\flashme\flashmev8a_noauto_tv\repack\arm7_boot_code_flashme.bin || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\arm9_boot_code.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\arm7_boot_code.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\arm9_gui_code.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\arm7_wifi_code.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\data_gfx.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\data_gfx.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\arm9_boot_code_flashme.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\arm9_boot_code_flashme.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_noauto_tv\unpack\arm7_boot_code_flashme.bin" ".temp\flashme\flashmev8a_noauto_tv\repack\arm7_boot_code_flashme.bin" >nul || exit /b 1

rem FlashMe v8a standard dump: plaintext extraction, recompression, and plaintext comparison.
.\release\dsfwtool.exe -i .\firmware\flashmev8a_st.bin -o .temp\flashme\flashmev8a_st\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\firmware\flashmev8a_st.bin -h .temp\flashme\flashmev8a_st\unpack\header.bin -fh .temp\flashme\flashmev8a_st\unpack\header_flashme.bin -p1 -decrypt -uncomp .temp\flashme\flashmev8a_st\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\flashme\flashmev8a_st\unpack\arm7_boot_code.bin -p3 -uncomp .temp\flashme\flashmev8a_st\unpack\arm9_gui_code.bin -p4 -uncomp .temp\flashme\flashmev8a_st\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\flashme\flashmev8a_st\unpack\data_gfx.bin -fp1 -uncomp .temp\flashme\flashmev8a_st\unpack\arm9_boot_code_flashme.bin -fp2 -uncomp .temp\flashme\flashmev8a_st\unpack\arm7_boot_code_flashme.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\flashme\flashmev8a_st\repack.bin -h .temp\flashme\flashmev8a_st\unpack\header.bin -fh .temp\flashme\flashmev8a_st\unpack\header_flashme.bin -p1 -comp -encrypt .temp\flashme\flashmev8a_st\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\flashme\flashmev8a_st\unpack\arm7_boot_code.bin -p3 -comp -flashme .temp\flashme\flashmev8a_st\unpack\arm9_gui_code.bin -p4 -comp -flashme .temp\flashme\flashmev8a_st\unpack\arm7_wifi_code.bin -p5 -comp -flashme .temp\flashme\flashmev8a_st\unpack\data_gfx.bin -fp1 -comp .temp\flashme\flashmev8a_st\unpack\arm9_boot_code_flashme.bin -fp2 -comp .temp\flashme\flashmev8a_st\unpack\arm7_boot_code_flashme.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\flashme\flashmev8a_st\repack.bin -h .temp\flashme\flashmev8a_st\repack\header.bin -fh .temp\flashme\flashmev8a_st\repack\header_flashme.bin -p1 -decrypt -uncomp .temp\flashme\flashmev8a_st\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\flashme\flashmev8a_st\repack\arm7_boot_code.bin -p3 -uncomp .temp\flashme\flashmev8a_st\repack\arm9_gui_code.bin -p4 -uncomp .temp\flashme\flashmev8a_st\repack\arm7_wifi_code.bin -p5 -uncomp .temp\flashme\flashmev8a_st\repack\data_gfx.bin -fp1 -uncomp .temp\flashme\flashmev8a_st\repack\arm9_boot_code_flashme.bin -fp2 -uncomp .temp\flashme\flashmev8a_st\repack\arm7_boot_code_flashme.bin || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\arm9_boot_code.bin" ".temp\flashme\flashmev8a_st\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\arm7_boot_code.bin" ".temp\flashme\flashmev8a_st\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\arm9_gui_code.bin" ".temp\flashme\flashmev8a_st\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\arm7_wifi_code.bin" ".temp\flashme\flashmev8a_st\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\data_gfx.bin" ".temp\flashme\flashmev8a_st\repack\data_gfx.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\arm9_boot_code_flashme.bin" ".temp\flashme\flashmev8a_st\repack\arm9_boot_code_flashme.bin" >nul || exit /b 1
fc /b ".temp\flashme\flashmev8a_st\unpack\arm7_boot_code_flashme.bin" ".temp\flashme\flashmev8a_st\repack\arm7_boot_code_flashme.bin" >nul || exit /b 1

rem iQue V2 FlashMe: plaintext extraction, recompression, and plaintext comparison.
.\release\dsfwtool.exe -i .\iquev2_flashme.bin -o .temp\flashme\iquev2_flashme\info.txt || exit /b 1
.\release\dsfwtool.exe -x .\iquev2_flashme.bin -h .temp\flashme\iquev2_flashme\unpack\header.bin -fh .temp\flashme\iquev2_flashme\unpack\header_flashme.bin -p1 -decrypt -uncomp .temp\flashme\iquev2_flashme\unpack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\flashme\iquev2_flashme\unpack\arm7_boot_code.bin -p3 -uncomp .temp\flashme\iquev2_flashme\unpack\arm9_gui_code.bin -p4 -uncomp .temp\flashme\iquev2_flashme\unpack\arm7_wifi_code.bin -p5 -uncomp .temp\flashme\iquev2_flashme\unpack\data_gfx.bin -fp1 -uncomp .temp\flashme\iquev2_flashme\unpack\arm9_boot_code_flashme.bin -fp2 -uncomp .temp\flashme\iquev2_flashme\unpack\arm7_boot_code_flashme.bin || exit /b 1
.\release\dsfwtool.exe -c .temp\flashme\iquev2_flashme\repack.bin -h .temp\flashme\iquev2_flashme\unpack\header.bin -fh .temp\flashme\iquev2_flashme\unpack\header_flashme.bin -p1 -comp -encrypt .temp\flashme\iquev2_flashme\unpack\arm9_boot_code.bin -p2 -comp -encrypt .temp\flashme\iquev2_flashme\unpack\arm7_boot_code.bin -p3 -comp -flashme .temp\flashme\iquev2_flashme\unpack\arm9_gui_code.bin -p4 -comp -flashme .temp\flashme\iquev2_flashme\unpack\arm7_wifi_code.bin -p5 -comp -flashme .temp\flashme\iquev2_flashme\unpack\data_gfx.bin -fp1 -comp .temp\flashme\iquev2_flashme\unpack\arm9_boot_code_flashme.bin -fp2 -comp .temp\flashme\iquev2_flashme\unpack\arm7_boot_code_flashme.bin || exit /b 1
.\release\dsfwtool.exe -x .temp\flashme\iquev2_flashme\repack.bin -h .temp\flashme\iquev2_flashme\repack\header.bin -fh .temp\flashme\iquev2_flashme\repack\header_flashme.bin -p1 -decrypt -uncomp .temp\flashme\iquev2_flashme\repack\arm9_boot_code.bin -p2 -decrypt -uncomp .temp\flashme\iquev2_flashme\repack\arm7_boot_code.bin -p3 -uncomp .temp\flashme\iquev2_flashme\repack\arm9_gui_code.bin -p4 -uncomp .temp\flashme\iquev2_flashme\repack\arm7_wifi_code.bin -p5 -uncomp .temp\flashme\iquev2_flashme\repack\data_gfx.bin -fp1 -uncomp .temp\flashme\iquev2_flashme\repack\arm9_boot_code_flashme.bin -fp2 -uncomp .temp\flashme\iquev2_flashme\repack\arm7_boot_code_flashme.bin || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\arm9_boot_code.bin" ".temp\flashme\iquev2_flashme\repack\arm9_boot_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\arm7_boot_code.bin" ".temp\flashme\iquev2_flashme\repack\arm7_boot_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\arm9_gui_code.bin" ".temp\flashme\iquev2_flashme\repack\arm9_gui_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\arm7_wifi_code.bin" ".temp\flashme\iquev2_flashme\repack\arm7_wifi_code.bin" >nul || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\data_gfx.bin" ".temp\flashme\iquev2_flashme\repack\data_gfx.bin" >nul || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\arm9_boot_code_flashme.bin" ".temp\flashme\iquev2_flashme\repack\arm9_boot_code_flashme.bin" >nul || exit /b 1
fc /b ".temp\flashme\iquev2_flashme\unpack\arm7_boot_code_flashme.bin" ".temp\flashme\iquev2_flashme\repack\arm7_boot_code_flashme.bin" >nul || exit /b 1


echo All dsfwtool tests passed.
exit /b 0

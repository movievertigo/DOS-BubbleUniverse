@echo off
set APP=bubblef
..\djgpp\bin\i586-pc-msdosdjgpp-gcc -O3 -march=i486 -mtune=i486 -falign-functions=16 -falign-loops=16 -fomit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections mainfixed.c -o %APP%.exe
..\djgpp\i586-pc-msdosdjgpp\bin\strip --strip-all %APP%.exe
..\djgpp\i586-pc-msdosdjgpp\bin\exe2coff %APP%.exe
copy /b ..\djgpp\cwsdstub.exe + %APP% %APP%.exe > nul
del %APP%
..\upx\upx --ultra-brute %APP%.exe

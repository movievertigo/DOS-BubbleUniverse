@echo off
set APP=bubble
..\djgpp\bin\i586-pc-msdosdjgpp-gcc -O3 -ffunction-sections -fdata-sections -Wl,--gc-sections main.c -o %APP%.exe
..\djgpp\i586-pc-msdosdjgpp\bin\strip --strip-all %APP%.exe
..\djgpp\i586-pc-msdosdjgpp\bin\exe2coff %APP%.exe
copy /b ..\djgpp\cwsdstub.exe + %APP% %APP%.exe > nul
del %APP%
..\upx\upx --ultra-brute %APP%.exe

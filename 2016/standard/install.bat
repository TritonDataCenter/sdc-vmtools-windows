REM This file installs the tools into c:\smartdc 
REM and sets c:\smartdc\bin as part of PATH

ECHO OFF
TITLE SDC Windows VM Tools - Install

cd %~dp0

CLS
ECHO SDC Windows VM Tools - Install
ECHO =======================================================
ECHO 
ECHO This tool will install the tools and drivers necessary
ECHO to run Windows in a SmartDC or SmartOS environment
ECHO The tools and drivers will be installed to c:\smartdc
ECHO 

PAUSE

mkdir c:\smartdc
xcopy * c:\smartdc /E /Q /Y
xcopy sysprep\SetupComplete.cmd %WINDIR%\Setup\Scripts\
xcopy /Y C:\smartdc\images\"Joyent Wiki.url" C:\Users\Default\Desktop\
xcopy /Y C:\smartdc\images\"Joyent Support.url" C:\Users\Default\Desktop\
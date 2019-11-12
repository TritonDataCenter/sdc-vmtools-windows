@echo off
>c:\smartdc\setup_output.txt (
  for /f "delims=" %%a in ('C:\smartdc\bin\mdata-get.exe administrator_pw') do @set password=%%a
  net user administrator "%password%"
  diskpart /s C:\smartdc\lib\diskpart_commands
  PowerShell.exe -ExecutionPolicy UnRestricted -File C:\smartdc\lib\CustomSetupSteps.ps1
)

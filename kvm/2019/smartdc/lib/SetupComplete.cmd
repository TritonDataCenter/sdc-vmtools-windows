@echo off
>c:\smartdc\setup_output.txt (
  diskpart /s C:\smartdc\lib\diskpart_commands
  PowerShell.exe -ExecutionPolicy UnRestricted -File C:\smartdc\lib\CustomSetupSteps.ps1
)

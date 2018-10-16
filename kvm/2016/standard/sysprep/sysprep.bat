@echo off 

REM The sysprep batch file will run sysprep against the unattend.xml
REM file, which has all the instructions required to turn the system into
REM a "gold master" image.

c:\windows\system32\sysprep\sysprep /generalize /oobe /shutdown /unattend:c:\smartdc\sysprep\windows2008r2.xml

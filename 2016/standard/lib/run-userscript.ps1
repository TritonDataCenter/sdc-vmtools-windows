$joy_userscript = (C:\smartdc\bin\mdata-get.exe user-script)

if ($joy_userscript -ne "No metadata for user-script") {
  powershell.exe -command "$joy_userscript"
}
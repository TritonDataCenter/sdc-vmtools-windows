$joy_userdata = (C:\smartdc\bin\mdata-get.exe user-data)

if ($joy_userdata -ne "No metadata for user-data") {
  $joy_userdata | Out-File C:\smartdc\tmp\mdata-user-data
}
Write-Host "Custom steps"

$vmalias = & "C:\smartdc\bin\mdata-get.exe" "sdc:alias"
$administrator_pw = & "C:\smartdc\bin\mdata-get.exe" "administrator_pw"

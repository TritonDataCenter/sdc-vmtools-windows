# Turn on RDP
set-ItemProperty -Path 'HKLM:System\CurrentControlSet\Control\Terminal Server' -name "fDenyTSConnections" -Value 0
Enable-NetFirewallRule -DisplayGroup "Remote Desktop"

# Turn on Ping
Set-NetFirewallRule -DisplayName "File and Printer Sharing (Echo Request - ICMPv4-In)" -enabled True
Set-NetFirewallRule -DisplayName "File and Printer Sharing (Echo Request - ICMPv6-In)" -enabled True

# Install NFS Client and .Net 3.5
Install-WindowsFeature -Name NFS-Client
Install-WindowsFeature Net-Framework-Core -source D:\sources\sxs\

# WinRM Settings
winrm set winrm/config/winrs '@{MaxMemoryPerShellMB="500"}'
winrm set winrm/config '@{MaxTimeoutms="1800000"}'

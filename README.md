# sdc-vmtools-windows

## Create your Windows VM and install Windows

Create your vmspec.json which is the configuration for your Windows VM:
```
{
  "brand": "kvm",
  "alias": "wsserver1",
  "hostname": "wsserver1",
  "autoboot": false,
  "ram": 4096,
  "max_physical_memory": 4096,
  "quota": 40,
  "disks": [
    {
      "boot": true,
      "model": "virtio",
      "size": 40960
    }
  ],
  "nics": [
    {
      "interface": "net0",
      "nic_tag": "external",
      "ip": "8.12.12.1",
      "primary": "true",
      "netmask": "255.255.255.0",
      "gateway": "8.12.12.1",
      "vlan_id": 100,
      "model": "virtio",
      "network_uuid": "2175011c-51c4-457z-8312-24ddbfdd18d0"
    }
  ]
}
```

Run ```vmadm create -f vmspec.json``` which will create the VM and tell you the UUID.

Copy your windows.iso, and drivers.iso (Latest virtio drivers) to the filesystem of the VM you just created, and tell it to boot from that:

```
# cp windows2012.iso drivers.iso /zones/UUID/root
# vmadm boot UUID order=cd,once=d cdrom=/windows2012.iso,ide cdrom=/drivers.iso,ide
```
Get the VNC information using ``vmadm info UUID vnc``` then use your VNC tool (Chicken of the VNC) to connect to the VM.

Go through the installer.  When it comes to the load drivers screen browse to the drivers.iso cdrom and load the network and disk driver.  It will then find a 40G drive to install on.

When it boots for the first time it will ask you to change the Administrator password. Set it to whatever you want. For customers this will be changed at boot time by calling mdata-get in the windows tools.

## Customize your Windows VM and run Sysprep

At this point Windows is installed, booted, and you are ready to customize your Windows VM.  Copy the sdc-vmtools-windows repository contents to your VM.  You'll likely need to create an ISO of this repository and then boot with it attached like we did above so it will be mounted in.

Once it's in your filesystem run the install.bat file.  This will install everything to C:\smartdc, install SetupComplete.cmd, and run the run-configuration.ps1.  The run-configuration.ps1 script sets up WinRM, enables remote desktop, enables ICMP, and installs NFS client for you so you don't have to go in and configure it manually.  It's always worth it to double check after that everything configured properly.  If RDP and ICMP aren't enabled properly now then you won't be able to get into the image once it's all up and running.

Edit C:\smartdc\bin\setup.bat and insert your MS key in place of ```XXXXX-XXXXX-XXXXX-XXXXX-XXXXX```

Make any other customizations you want for your image.

Run ```C:\smartdc\sysprep\sysprep.bat``` which will sysprep the image and shut down the VM.

## Create the image file and image json manifest

After the VM is completely shut down, snapshot it, send it to a file and gzip it:

```
# zfs snapshot UUID-disk0@final
# zfs send UUID-disk0@final > ws2012-1.0.0.zfs && gzip ws2012-1.0.0.zfs
```

Create your manifest for the image.  It should look like this below replacing with your own created UUID, insert the file size of the image file above, and the sha1 of the image file above using ```digest -a sha1 ws2012-1.0.0.zfs.gz```

```
{
  "v": "2",
  "name": "ws2012",
  "cpu_type": "host",
  "version": "20180502",
  "type": "zvol",
  "cpu_type": "host",
  "state": "active",
  "disabled": false,
  "public": true,
  "description": "Windows Server 2012 Standard 64-bit image.",
  "homepage": "http://wiki.joyent.com/jpc2/Windows+Server+2012+Standard",
  "os": "windows",
  "image_size": "40960",
  "files": [
    {
      "sha1": "99e8e515ba66792ef0c4f10cf992f68e7c9c9ca2",
      "size": 5438204051,
      "compression": "gzip"
    }
  ],
  "requirements": {
    "min_ram": 4096,
    "networks": [
      {
        "name": "net0",
        "description": "public"
      }
    ]
  },
  "users": [
    {
      "name": "administrator"
    }
  ],
  "tags": {
    "role": "os"
  },
  "billing_tags": [
    "windows"
  ],
  "disk_driver": "virtio",
  "nic_driver": "virtio",
  "uuid": "2cb8015a-4e3c-11e8-b313-6f4db36e4ee2",
  "owner": "00000000-0000-0000-0000-000000000000",
  "urn": "sdc:admin:ws2012:20180502"
}
```

You can then import the image using ```sdc-imgadm import -f ws2012-1.0.0.zfs.gz -m ws2012-1.0.0.json```

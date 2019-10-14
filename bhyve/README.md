# ISO files repo for setting up Windows unattended on bhyve / SmartOS
For [Windows Server eval ISOs](https://www.microsoft.com/en-us/evalcenter/evaluate-windows-server-2019) remove the `<ProductKey>` XML tag from `Autounattend.xml` ([lines 50-53](https://github.com/joyent/sdc-vmtools-windows/blob/9d1d075171a6c93244cd8487ad94aa431b7f761e/bhyve/Autounattend.xml#L50-L53)) to make unattended Windows Server setup work (License Terms not found error), then create a fresh `winsetup.iso`. See below.

For Windows 10 you need the `<ProductKey>` tag, you could use [Joyent's latest prepared iso](https://download.joyent.com/pub/vmtools/winsetup-2012-2016-20180927.iso)

Find versions of this ISO at https://download.joyent.com/pub/vmtools/winsetup*

Or on OSX make your own:
```
git clone https://github.com/joyent/sdc-vmtools-windows
hdiutil makehybrid -o winsetup.iso sdc-vmtools-windows/bhyve/ -iso -joliet
```

This ISO currently sets up a full Windows install with v0.1.141 virtio drivers
for disk and networking, SAC console, ICMP ping, and RDP enabled.

On a SmartOS machine with the newest platform given to you by Joyent,
first set some variables for the bhyve VM used for Windows installation:

```
export WINDOWS_INSTALL_CD=/zones/win2019eval.iso
export WINDOWS_DRIVER_CD=/zones/winsetup.iso
```

next:
```
zfs create -V 80G zones/windows
```

For Windows Server 2016 / 2019 and Windows 10:

```
pfexec /usr/sbin/bhyve -c 2 -m 3G -H \
    -l com1,stdio \
    -l bootrom,/usr/share/bhyve/uefi-rom.bin \
    -s 2,ahci-cd,$WINDOWS_INSTALL_CD \
    -s 3,virtio-blk,/dev/zvol/rdsk/zones/windows \
    -s 4,ahci-cd,$WINDOWS_DRIVER_CD \
    -s 31,lpc \
    windows
```

For Windows Server 2012:

```
pfexec /usr/sbin/bhyve -c 2 -m 3G -H \
    -l com1,stdio \
    -l bootrom,/usr/share/bhyve/uefi-rom.bin \
    -s 2,virtio-blk,/dev/zvol/rdsk/zones/windows \
    -s 3,ahci-cd,$WINDOWS_INSTALL_CD \
    -s 4,ahci-cd,$WINDOWS_DRIVER_CD \
    -s 31,lpc \
    windows
```

If you want VNC access, add the following line before `windows`
```
    -s 28,fbuf,vga=off,tcp=0.0.0.0:5900,w=1024,h=768,wait -s 29,xhci,tablet \
```

Now wait until bhyve terminates, which will take some time.

While Windows is doing its initial install phase, the Windows Special
Administrative Console will be displayed. If you'd rather watch the scrolling
install log rather than a blank screen, hit `p` to disable paging and hit
`<esc><tab>` to switch to the install log when the `SACSetupAct` message appears.

When bhyve terminates, run it again, but without the `WINDOWS_INSTALL_CD` line:

For Windows Server 2016 / 2019 and Windows 10:

```
pfexec /usr/sbin/bhyve -c 2 -m 3G -H \
    -l com1,stdio \
    -l bootrom,/usr/share/bhyve/uefi-rom.bin \
    -s 3,virtio-blk,/dev/zvol/rdsk/zones/windows \
    -s 4,ahci-cd,$WINDOWS_DRIVER_CD \
    -s 31,lpc \
    windows
```

For Windows Server 2012:

```
pfexec /usr/sbin/bhyve -c 2 -m 3G -H \
    -l com1,stdio \
    -l bootrom,/usr/share/bhyve/uefi-rom.bin \
    -s 2,virtio-blk,/dev/zvol/rdsk/zones/windows \
    -s 4,ahci-cd,$WINDOWS_DRIVER_CD \
    -s 31,lpc \
    windows
```

And wait for bhyve to terminate again. It will take a while, but not as long
as the first phase. We now move installation into a zone and the vmadm/imgadm
ecosystem. The initial installation phases above are done directly with bhyve in
the global zone since vmadm does not (yet) support the 'once' vmadm arg for
bhyve.

Now we create our image:

```
zfs send zones/windows | tee /zones/windows.zvol | digest -a sha1
zfs destroy zones/windows
/usr/sbin/bhyvectl --destroy --vm=windows
ls -l /zones/windows.zvol
```

Using the SHA1 hash and byte size from `ls -l`, fill in windows.imgmanifest:

```
{
    "v": 2,
    "uuid": "738dccbc-b1b6-11e8-bd8a-ab7098639442",
    "name": "windows-installing",
    "version": "0.0.1",
    "type": "zvol",
    "os": "windows",
    "files": [ {
        "sha1": "<windows.zvol SHA1 hash>",
        "size": <file size in byves>,
        "compression": "none"
    } ]
}
```

Then:

`imgadm install -m /zones/windows.imgmanifest -f /zones/windows.zvol`

You now have an imgadm image which can be used with `vmadm`; you can delete
`windows.imgmanifest` and `windows.zvol`. If you have a bunch of older images in
imgadm, you can clean them up with `imgadm vacuum`, which remove all images
that are not being currently used by a zone. Be careful you don't accidentally
vacuum your new image too!

Here's a JSON useful for `vmadm create` which uses the above image:

```
{
  "brand": "bhyve",
  "vcpus": 2,
  "autoboot": false,
  "ram": 3072,
  "bootrom": "/usr/share/bhyve/uefi-rom.bin",
  "disks": [ {
    "boot": true,
    "model": "virtio",
    "image_uuid": "738dccbc-b1b6-11e8-bd8a-ab7098639442",
    "image_size": 15360
  } ],
  "nics": [ {
    "nic_tag": "admin",
    "model": "virtio",
    "ip": "10.88.88.69",
    "netmask": "255.255.255.0",
    "gateway": "10.88.88.2"
  } ],
  "resolvers": ["1.1.1.1", "1.0.0.1"]
}
```

Put that in a JSON file (e.g. `windows.json`), and adjust the networking to taste.
Then creat a new VM and start it:
```
vmadm create -f windows.json
vmadm start <new VM's UUID>
```

RDP should become available once Windows boots up. Alternatively, use VNC,
or `vmadm console` to access Windows' SAC.

RDP available via external NIC. If put only on admin NIC, you could use VNC with socat like this:
```
socat TCP-LISTEN:5500 EXEC:'ssh cn06 "socat STDIO UNIX-CONNECT:/zones/cb733c87-5b5f-e0d5-feef-ff42c6519117/root/tmp/vm.vnc"'
```

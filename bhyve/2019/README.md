# Windows Server 2019 bhyve

* [Windows Server 2019 bhyve](#windows-server-2019-bhyve)
  * [Creating a fresh bhyve image](#creating-a-fresh-bhyve-image)
  * [Updating bhyve image](#updating-bhyve-image)
  * [Resource notes](#resource-notes)

## Creating a fresh bhyve image

Joyent sdc-vmtools-windows documentation for Windows Server 2012R2 and 2016 involve hard-coding configuration values, which has longevity limitations. As such, this process relies on operator judgement, allows infrastructure agnostic configuration, and decouples image creation from winsetup ISO publishes.

To simplify SDC/Triton image tooling, the headnode will be used for the VM.

1. SSH to headnode, `ssh root@10.70.1.2`
2. Launch a new `screen` session
    * keep this session open until image creation is completed
3. Create working directory for images, `mkdir -p /var/tmp/images`
4. Set helper variables

    ```bash
    export WINDOWS_INSTALL_CD=/zones/win2019.iso
    export VIRTIO_DRIVER_CD=/zones/virtio-win.iso
    export INSTALL_ZVOL=windows_2019
    ```

5. Copy Windows ISO to headnode, `scp ~/Downloads/SW_DVD9_Win_Server_STD_CORE_2019_64Bit_English_DC_STD_MLF_X21-96581.iso root@10.70.1.2:/var/tmp/images/win2019.iso`
6. Download VirtIO drivers, `wget --no-check-certificate -O $VIRTIO_DRIVER_CD https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso`
    * As of this writing, `171` is the most recent stable
7. Move Windows install ISO to accessible location, `mv /var/tmp/win2019.iso $WINDOWS_INSTALL_CD`
8. Create installation volume, `zfs create -V 60G zones/$INSTALL_ZVOL`

Launch bhyve with installation media

```bash
pfexec /usr/sbin/bhyve -c 4 -m 8G -H \
    -l com1,stdio \
    -l bootrom,/usr/share/bhyve/uefi-rom.bin \
    -s 2,ahci-cd,$WINDOWS_INSTALL_CD \
    -s 3,virtio-blk,/dev/zvol/rdsk/zones/$INSTALL_ZVOL \
    -s 4,ahci-cd,$VIRTIO_DRIVER_CD \
    -s 28,fbuf,vga=off,tcp=0.0.0.0:15902,w=1024,h=768,wait \
    -s 29,xhci,tablet \
    -s 31,lpc \
    $INSTALL_ZVOL
```

Connect via TightVNC (POST will wait for VNC connection), then hit any key for Windows installation. During Windows installation, load NetKVM driver first, then load viostor. Once the installation is done, the bhyve process will terminate.

Finalize installation with a boot, no user input required for this.

```bash
pfexec /usr/sbin/bhyve -c 4 -m 8G -H \
    -l com1,stdio \
    -l bootrom,/usr/share/bhyve/uefi-rom.bin \
    -s 3,virtio-blk,/dev/zvol/rdsk/zones/$INSTALL_ZVOL \
    -s 28,fbuf,vga=off,tcp=0.0.0.0:15902,w=1024,h=768 \
    -s 29,xhci,tablet \
    -s 31,lpc \
    $INSTALL_ZVOL
```

Re-run the above command, connect via VNC, set the [server administrator password](https://passwordmanager.lrscorp.net/SecretView.aspx?secretid=10). sign-in, then shutdown the VM.

Save a zvol for ease of use, `zfs send zones/$INSTALL_ZVOL > /zones/${INSTALL_ZVOL}_base.zvol`

Create a VM manifest:

```bash
echo '{
  "alias": "ops-dev-base03",
  "brand": "bhyve",
  "vcpus": 4,
  "autoboot": false,
  "ram": 8192,
  "bootrom": "uefi",
  "disks": [
    {
      "boot": true,
      "model": "virtio",
      "size": 61440
    }
  ],
  "nics": [
    {
      "nic_tag": "external",
      "vlan_id": 201,
      "ip": "10.70.1.221",
      "netmask": "255.255.255.0",
      "gateway": "10.70.1.1",
      "primary": "true",
      "model": "virtio"
    }
  ],
  "resolvers": [
    "10.70.7.2",
    "10.70.7.3"
  ]
}
' > /zones/${INSTALL_ZVOL}_manifest.json
```

Create a VM, and try to make vminfod less crashy: `vmadm create -f /zones/${INSTALL_ZVOL}_manifest.json && svcadm restart vminfod`

Copy base Windows install into fresh VM:

```bash
zfs destroy zones/$(vmadm lookup -1 alias=ops-dev-base03)/disk0 && \
zfs send zones/$INSTALL_ZVOL | zfs recv zones/$(vmadm lookup -1 alias=ops-dev-base03)/disk0 && \
vmadm start $(vmadm lookup -1 alias=ops-dev-base03)
```

bhyve VNC and vminfod is pretty inconsistent and unstable, so create a VNC socket manually
`socat -d -d TCP4-LISTEN:15902,fork UNIX-CONNECT:/zones/$(vmadm lookup -1 alias=ops-dev-base03)/root/tmp/vm.vnc`

Initial networking won't be functional, so login via VNC and manually set networking

```Batchfile
netsh interface ip set address "Ethernet" static 10.70.1.221 255.255.255.0 10.70.1.1 1
netsh interface ipv4 add dnsserver "Ethernet" address=10.70.7.2 index=1
```

Also disable the Windows Firewall, and enable Remote desktop connections. You can now switch to RDP.

Configure EMS

```Batchfile
bcdedit /ems on
bcdedit /emssettings emsport:1 emsbaudrate:115200
```

Configure prototype VM as desired, then proceed to [Updating the image](#updating-the-image), *Step 10* through *Step 15*

## Updating bhyve image

1. SSH to headnode, `root@10.70.1.2`
2. Launch a new `screen` session
    * keep this session open until image creation is completed
3. Set helper variables

    ```bash
    TEMPLATE_VM_ALIAS='ops-dev-base03' && \
    PATCH_VERSION=0 && \
    OS_VERSION=2019 && \
    HYPERVISOR=bhyve && \
    SSH_PUB=$(cat /root/.ssh/sdc.id_rsa.pub) && \
    ADMIN_UUID=$(sdc-useradm get admin | json uuid) && \
    IMAGE_UUID=$(uuid) && \
    VM_ID=$(vmadm lookup -1 alias=${TEMPLATE_VM_ALIAS}) && \
    VM_IP=$(vmadm get $VM_ID | json -a nics | json -a ip -c 'this.primary') && \
    V=$(date +"%y.%m.$PATCH_VERSION") && \
    echo -e "\n\nADMIN_UUID: $ADMIN_UUID\nIMAGE_UUID: $IMAGE_UUID\nVM_ID: $VM_ID\nVERSION: $V\nVM_IP: $VM_IP\n"
    ```

4. Take a pre-patch snapshot for a safety net, `vmadm stop $VM_ID; zfs snapshot zones/${VM_ID}/disk0@pre${V}`
    * Rollback to this point if any problems occur `vmadm stop -F $VM_ID && zfs rollback zones/${VM_ID}/disk0@pre${V} && vmadm start $VM_ID`
5. Start the template VM, `vmadm start $VM_ID`
6. RDP to `VM_IP` as `\administrator` with the credentials you specified during setup
7. Perform neccessary image customization
    * e.g. Patch and reboot via **Windows Update**, application updates
8. Clean up temporary files, such as
    * Windows patches: `Stop-Service wuauserv; Remove-Item -Recurse C:\Windows\SoftwareDistribution\Download; Start-Service wuauserv`
    * Any package installers
    * Recycle bin
9. Shutdown the VM via RDP
10. Take a snapshot to save non-sysprep version for rollback, <5 minutes

    ```bash
    cd /var/tmp/images/ && \
    zfs destroy zones/$VM_ID/disk0@presysprep; \
    zfs snapshot zones/$VM_ID/disk0@presysprep && \
    zfs send zones/$VM_ID/disk0@presysprep > "windows-server-${OS_VERSION}-${HYPERVISOR}-${V}.zvol" && \
    vmadm start $VM_ID
    ```

11. Once the VM starts back up, RDP back in and execute `c:\windows\system32\sysprep\sysprep /generalize /oobe /shutdown /unattend:c:\smartdc\lib\unattend.xml`
12. Sysprep will stop the VM, once the VM is stopped, continue to the next step. `vmadm list uuid=$VM_ID -Ho state`
13. Export sysprep version, and rollback to pre-sysprep snapshot, ~30 minutes `zfs destroy zones/$VM_ID/disk0@sysprep; zfs snapshot zones/$VM_ID/disk0@sysprep && zfs send zones/$VM_ID/disk0@sysprep | gzip -c > /var/tmp/images/ws${OS_VERSION}-$V-sysprep.zvol.gz && zfs destroy zones/$VM_ID/disk0@sysprep; zfs rollback zones/$VM_ID/disk0@presysprep && zfs destroy zones/$VM_ID/disk0@presysprep`
14. Create image manifest

    ```bash
    echo "{
      \"v\": \"2\",
      \"uuid\": \"$IMAGE_UUID\",
      \"owner\": \"$ADMIN_UUID\",
      \"name\": \"Windows Server ${OS_VERSION}\",
      \"description\": \"Windows Server ${OS_VERSION}, built $(date +"%Y-%m-%d")\",
      \"version\": \"$V\",
      \"state\": \"active\",
      \"disabled\": false,
      \"public\": true,
      \"os\": \"windows\",
      \"type\": \"zvol\",
      \"files\": [
        {
          \"sha1\": \"$(sum -x sha1 /var/tmp/images/ws${OS_VERSION}-$V-sysprep.zvol.gz | cut -d' ' -f1)\",
          \"size\": $(ls -l /var/tmp/images/ws${OS_VERSION}-$V-sysprep.zvol.gz | awk '{ print $5 }'),
          \"compression\": \"gzip\"
        }
      ],
      \"requirements\": {
        \"brand\": \"bhyve\",
        \"bootrom\": \"uefi\",
        \"networks\": [
          {
            \"name\": \"net0\",
            \"description\": \"public\"
          }
        ]
      },
      \"generate_passwords\": \"true\",
      \"users\": [
        {
          \"name\": \"administrator\"
        }
      ],
      \"image_size\": $(zfs get -H -o value -p volsize zones/$VM_ID/disk0 | awk '{print $1/1024/1024}'),
      \"disk_driver\": \"virtio\",
      \"nic_driver\": \"virtio\",
      \"cpu_type\": \"host\"
    }
    " > /var/tmp/images/ws${OS_VERSION}_vm.manifest
    ```

15. Import newly created image into bli-sdc02 `sdc-imgadm import --skip-owner-check -m /var/tmp/images/ws${OS_VERSION}_vm.manifest -f /var/tmp/images/ws${OS_VERSION}-$V-sysprep.zvol.gz`
16. Copy SSH key to headnodes for easier transfers

    ```bash
    for headnode in 10.91.254.4 10.80.254.2
    do
      ssh -i /root/.ssh/sdc.id_rsa $headnode "grep -qv '$SSH_PUB' /root/.ssh/authorized_keys && echo '$SSH_PUB' >> /root/.ssh/authorized_keys"
    done
    ```

17. Copy neccessary files to other headnodes, and import image:

    ```bash
    for headnode in 10.91.254.4 10.80.254.2
    do
      echo "Copying ws${OS_VERSION}-$V-sysprep.zvol.gz to $headnode" &&
      scp -i /root/.ssh/sdc.id_rsa /var/tmp/images/ws${OS_VERSION}-$V-sysprep.zvol.gz $headnode:/var/tmp/images/ &&
      echo "Copying ws${OS_VERSION}_vm.manifest to $headnode" &&
      scp -i /root/.ssh/sdc.id_rsa /var/tmp/images/ws${OS_VERSION}_vm.manifest $headnode:/var/tmp/images/ &&
      echo "Importing image" &&
      ssh -i /root/.ssh/sdc.id_rsa $headnode "/opt/smartdc/bin/sdc-imgadm import --skip-owner-check -m /var/tmp/images/ws${OS_VERSION}_vm.manifest -f /var/tmp/images/ws${OS_VERSION}-$V-sysprep.zvol.gz" &&
      echo "Removing old images on $headnode" &&
      ssh -i /root/.ssh/sdc.id_rsa $headnode "find /var/tmp/images -iname 'ws${OS_VERSION}-*.zvol.gz' -mtime +14 -exec rm {} \\;" &&
      echo -e "\n\n"
    done
    ```

18. Copy image files to backup server `USER=first.last; rsync -P /var/tmp/images/ws${OS_VERSION}-$V*.zvol* $USER@ops-sea-bkup01.faithlife.io:/mnt/data01/Operations/images/Windows/; ssh $USER@ops-sea-bkup01.faithlife.io "sudo chown -R ftp_user:ftp_users /mnt/data01/Operations/images && sudo chmod -R 777 /mnt/data01/Operations/images"`
19. Destroy safety net snapshot, `zfs destroy zones/${VM_ID}/disk0@pre$V`

## Resource notes

* <https://github.com/joyent/sdc-vmtools-windows>
* <https://gist.github.com/mgerdts/6fabc913aca3acd2f1e435a7dc2bbd80>

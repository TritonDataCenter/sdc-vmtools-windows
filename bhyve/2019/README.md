# Windows Server 2019 bhyve

* [Windows Server 2019 bhyve](#windows-server-2019-bhyve)
  * [Creating a fresh bhyve image](#creating-a-fresh-bhyve-image)
  * [Updating bhyve image](#updating-bhyve-image)
  * [Resource notes](#resource-notes)

## Creating a fresh bhyve image

Windows Server 2012R2 and 2016 involved hard-coding configuration values, this process relies on operator judgement, allows infrastructure agnostic configuration, and decouples image creation from winsetup ISO publishes.

To simplify SDC/Triton image tooling, the headnode will be used for the VM.

1. SSH to headnode, `ssh root@8.11.11.2`
2. Launch a new `screen` session
    * keep this session open until image creation is completed
3. Create working directory for images, `mkdir -p /var/tmp/images`
4. Set helper variables

    ```bash
    TEMPLATE_VM_ALIAS='win2019_bhyve' && \
    WINDOWS_INSTALL_CD=/zones/win2019.iso && \
    VIRTIO_DRIVER_CD=/zones/virtio-win.iso && \
    INSTALL_ZVOL=windows_bhyve_2019
    ```

5. Copy Windows ISO to headnode, `scp ~/Downloads/SW_DVD9_Win_Server_STD_CORE_2019_64Bit_English_DC_STD_MLF_X21-96581.iso root@8.11.11.2:/var/tmp/images/win2019.iso`
6. Move Windows install ISO to a bhyve-accessible location, `mv /var/tmp/images/win2019.iso $WINDOWS_INSTALL_CD`
7. Download VirtIO drivers, `wget --no-check-certificate -O $VIRTIO_DRIVER_CD https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso`
    * As of this writing, `171` is the most recent stable
8. Create installation volume, `zfs create -o volblocksize=4k -V 60G zones/$INSTALL_ZVOL`
9. Launch bhyve with installation media

    ```bash
    pfexec /usr/sbin/bhyve -c 4 -m 8G -H \
        -l com1,stdio \
        -l bootrom,/usr/share/bhyve/uefi-rom.bin \
        -s 2,ahci-cd,$WINDOWS_INSTALL_CD \
        -s 3,virtio-blk,/dev/zvol/rdsk/zones/$INSTALL_ZVOL \
        -s 4,ahci-cd,$VIRTIO_DRIVER_CD \
        -s 28,fbuf,vga=off,tcp=0.0.0.0:15900,w=1024,h=768,wait \
        -s 29,xhci,tablet \
        -s 31,lpc \
        $INSTALL_ZVOL
    ```

10. Connect via TightVNC to port `15900`, hit any key to boot Windows installation media
    * POST will wait for VNC connection
11. Go through the Windows installation process
    * TightVNC client is known to work
    * Choose **Custom** installation
    * Load VirtIO **NetKVM** then **viostor** drivers from the VirtIO ISO during setup
    * Once the installation is done, the bhyve process will terminate
12. Boot VM again to finalize Windows installation, no user input required for this

    ```bash
    pfexec /usr/sbin/bhyve -c 4 -m 8G -H \
        -l com1,stdio \
        -l bootrom,/usr/share/bhyve/uefi-rom.bin \
        -s 3,virtio-blk,/dev/zvol/rdsk/zones/$INSTALL_ZVOL \
        -s 28,fbuf,vga=off,tcp=0.0.0.0:15900,w=1024,h=768 \
        -s 29,xhci,tablet \
        -s 31,lpc \
        $INSTALL_ZVOL
    ```

13. Re-run the above command, re-connect via VNC, finalize setup, then shutdown
    * Set administrator password
14. Save a zvol for ease of use, `zfs send zones/$INSTALL_ZVOL > /var/tmp/images/${INSTALL_ZVOL}_base.zvol`
15. Clean up initial installation files
    1. `rm $WINDOWS_INSTALL_CD $VIRTIO_DRIVER_CD`
    2. `zfs destroy zones/$INSTALL_ZVOL`
16. Build template VM json, ensure values match your environment

    ```bash
    echo '{
      "alias": "win2019_bhyve",
      "brand": "bhyve",
      "vcpus": 2,
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
          "vlan_id": 111,
          "ip": "8.11.11.48",
          "netmask": "255.255.255.0",
          "gateway": "8.11.11.1",
          "primary": "true",
          "model": "virtio"
        }
      ],
      "resolvers": [
        "8.8.8.8"
      ]
    }
    ' > /var/tmp/images/${INSTALL_ZVOL}_manifest.json
    ```

17. Create template VM, `vmadm create -f /var/tmp/images/${INSTALL_ZVOL}_manifest.json && svcadm restart vminfod`
    * Depending on Platform Image, `vminfod` may need restarted to pick up bhyve changes
18. Copy base Windows install into fresh VM

    ```bash
    VM_ID=$(vmadm lookup -1 alias=${TEMPLATE_VM_ALIAS})
    zfs destroy zones/${VM_ID}/disk0 && \
    zfs send zones/$INSTALL_ZVOL | zfs recv zones/${VM_ID}/disk0 && \
    vmadm start $VM_ID
    ```

19. Create VNC socket manually, `socat -d -d TCP4-LISTEN:15900,fork UNIX-CONNECT:/zones/${VM_ID}/root/tmp/vm.vnc`
    * [bhyve VNC and vminfod can be inconsistent](https://smartos.org/bugview/OS-7953)
20. Connect via TightVNC and sign-in to VM
21. Manually configure initial networking, ensure values match your environment
    * Unlike KVM, bhyve does not listen to local traffic for DHCP requests

    ```Batchfile
    netsh interface ip set address "Ethernet" static 8.11.11.48 255.255.255.0 8.11.11.1 1
    netsh interface ip set dns "Ethernet" static 8.8.8.8
    ```

22. Allow Remote desktop connection
23. Abort the `socat` command and switch from VNC to RDP
24. Rename hostname to match `TEMPLATE_VM_ALIAS` value
25. Enable console redirection, this allows `vmadm console $VM_ID` to be used

    ```Batchfile
    bcdedit /ems on
    bcdedit /emssettings emsport:1 emsbaudrate:115200
    ```

26. Copy [smartdc](./smartdc) to VM's `C:\smartdc`
27. Copy [SetupComplete.cmd](./smartdc/lib/SetupComplete.cmd) to `C:\Windows\Setup\Scripts\SetupComplete.cmd`
28. Set hardware time to UTC, `reg ADD HKLM\System\CurrentControlSet\Control\TimeZoneInformation /t REG_DWORD /v RealTimeIsUniversal /d 1`
29. Perform neccessary image customization
    * e.g. patching, sysprep unattend.xml changes
30. Continue with [updating bhyve image](#updating-bhyve-image)

## Updating bhyve image

1. SSH to headnode, `root@8.11.11.2`
2. Launch a new `screen` session
    * keep this session open until image creation is completed
3. Set helper variables

    ```bash
    TEMPLATE_VM_ALIAS='win2019_bhyve' && \
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
    * Rollback to this point if any problems occur, `vmadm stop -F $VM_ID && zfs rollback zones/${VM_ID}/disk0@pre${V} && vmadm start $VM_ID`
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

11. Once the VM starts back up, RDP back in and execute, `C:\Windows\System32\sysprep\sysprep /generalize /oobe /shutdown /unattend:c:\smartdc\lib\unattend.xml`
12. Sysprep will stop the VM, once the VM is stopped, continue to the next step, `vmadm list uuid=$VM_ID -Ho state`
13. Export sysprep version, and rollback to pre-sysprep snapshot, ~30 minutes

    ```bash
    zfs destroy zones/$VM_ID/disk0@sysprep; \
    zfs snapshot zones/$VM_ID/disk0@sysprep && \
    zfs send zones/$VM_ID/disk0@sysprep | gzip -c > /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}-sysprep.zvol.gz && \
    zfs destroy zones/$VM_ID/disk0@sysprep && \
    zfs rollback zones/$VM_ID/disk0@presysprep && \
    zfs destroy zones/$VM_ID/disk0@presysprep
    ```

14. Create image manifest

    ```bash
    echo "{
      \"v\": \"2\",
      \"uuid\": \"$IMAGE_UUID\",
      \"owner\": \"$ADMIN_UUID\",
      \"name\": \"Windows Server ${OS_VERSION} - ${HYPERVISOR}\",
      \"description\": \"Windows Server ${OS_VERSION} - ${HYPERVISOR}, built $(date +"%Y-%m-%d")\",
      \"version\": \"${V}\",
      \"state\": \"active\",
      \"disabled\": false,
      \"public\": true,
      \"os\": \"windows\",
      \"type\": \"zvol\",
      \"files\": [
        {
          \"sha1\": \"$(sum -x sha1 /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}-sysprep.zvol.gz | cut -d' ' -f1)\",
          \"size\": $(ls -l /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}-sysprep.zvol.gz | awk '{ print $5 }'),
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
      \"image_size\": $(zfs get -H -o value -p volsize zones/${VM_ID}/disk0 | awk '{print $1/1024/1024}'),
      \"disk_driver\": \"virtio\",
      \"nic_driver\": \"virtio\",
      \"cpu_type\": \"host\"
    }
    " > /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}.manifest
    ```

15. Import newly created image into local headnode

    ```bash
    sdc-imgadm import --skip-owner-check -m /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}.manifest -f /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}-sysprep.zvol.gz
    ```

16. Destroy safety net snapshot, `zfs destroy zones/${VM_ID}/disk0@pre${V}`

## Resource notes

* <https://gist.github.com/mgerdts/6fabc913aca3acd2f1e435a7dc2bbd80>

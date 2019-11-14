# Windows Server 2019 KVM

* [Windows Server 2019 KVM](#windows-server-2019-kvm)
  * [Creating a fresh KVM image](#creating-a-fresh-kvm-image)
  * [Updating kvm image](#updating-kvm-image)
  * [Resource notes](#resource-notes)

## Creating a fresh KVM image

Windows Server 2012R2 and 2016 involved hard-coding configuration values, this process relies on operator judgement, allows infrastructure agnostic configuration, and decouples image creation from winsetup ISO publishes.

To simplify SDC/Triton image tooling, the headnode will be used for the VM.

1. SSH to headnode, `ssh root@8.11.11.2`
2. Launch a new `screen` session
    * keep this session open until image creation is completed
3. Create working directory for images, `mkdir -p /var/tmp/images`
4. Copy Windows ISO to headnode, `scp ~/Downloads/SW_DVD9_Win_Server_STD_CORE_2019_64Bit_English_DC_STD_MLF_X21-96581.iso root@10.70.1.2:/var/tmp/images/win2019.iso`
5. Download VirtIO drivers, `wget --no-check-certificate -O /var/tmp/images/virtio-win.iso https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso`
    * As of this writing, `171` is the most recent stable
6. Build template VM json, ensure values match your environment

    ```bash
    echo '{
      "alias": "win2019_kvm",
      "brand": "kvm",
      "vcpus": 4,
      "qemu_extra_opts": "-cpu host",
      "autoboot": false,
      "ram": 8192,
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
          "ip": "8.12.12.49",
          "netmask": "255.255.255.0",
          "gateway": "8.12.12.1",
          "primary": "true",
          "model": "virtio"
        }
      ],
      "resolvers": [
        "8.8.8.8"
      ]
    }' > /var/tmp/images/windows_server_2019_kvm.json
    ```

7. Create template VM, `vmadm create -f /var/tmp/images/windows_server_2019_kvm.json`
8. Set helper variables

    ````bash
    TEMPLATE_VM_ALIAS='win2019_kvm' && \
    VM_ID=$(vmadm lookup -1 alias=${TEMPLATE_VM_ALIAS})
    ```

9. Copy iso images to VM zone root, `cp /var/tmp/images/{virtio-win,win2019}.iso /zones/${VM_ID}/root/ && chmod 666 /zones/${VM_ID}/root/*.iso`
10. Boot VM for initial install, `vmadm start -v $VM_ID order=cd,once=d cdrom=/win2019.iso,ide cdrom=/virtio-win.iso,ide && vmadm info $VM_ID | json vnc`
11. Connect with VNC information and go through the Windows installation process
    * TightVNC client is known to work
    * Choose **Custom** installation
    * Load VirtIO **NetKVM** then **viostor** drivers from the VirtIO ISO during setup
12. Allow Remote desktop connections
13. Rename hostname to match `TEMPLATE_VM_ALIAS` value
14. Shutdown VM from inside VNC session
15. Start VM normally, `vmadm start $VM_ID`
16. Connect via RDP instead of VNC
17. Copy [smartdc](./smartdc) to VM's `C:\smartdc`
18. Copy [SetupComplete.cmd](./smartdc/lib/SetupComplete.cmd) to `C:\Windows\Setup\Scripts\SetupComplete.cmd`
19. Set hardware time to UTC, `reg ADD HKLM\System\CurrentControlSet\Control\TimeZoneInformation /t REG_DWORD /v RealTimeIsUniversal /d 1`
20. Perform neccessary image customization
    * e.g. patching, sysprep unattend.xml changes
21. Continue with [updating kvm image](#updating-kvm-image)

## Updating kvm image

1. SSH to headnode, `ssh root@10.70.1.2`
2. Launch a new `screen` session
    * keep this session open until process is completed
3. Set helper variables

    ```bash
    TEMPLATE_VM_ALIAS='win2019_kvm' && \
    PATCH_VERSION=0 && \
    OS_VERSION=2019 && \
    HYPERVISOR=kvm && \
    SSH_PUB=$(cat /root/.ssh/sdc.id_rsa.pub) && \
    ADMIN_UUID=$(sdc-useradm get admin | json uuid)  && \
    IMAGE_UUID=$(uuid) && \
    VM_ID=$(vmadm lookup -1 alias=${TEMPLATE_VM_ALIAS}) && \
    VM_IP=$(vmadm get $VM_ID | json -a nics | json -a ip -c 'this.primary') && \
    V=$(date +"%y.%m.${PATCH_VERSION}") && \
    echo -e "\n\nADMIN_UUID: ${ADMIN_UUID}\nIMAGE_UUID: ${IMAGE_UUID}\nVM_ID: ${VM_ID}\nVERSION: ${V}\nVM_IP: ${VM_IP}\n"
    ```

4. Take a pre-patch snapshot for a safety net, `zfs snapshot zones/${VM_ID}-disk0@pre${V}`
    * Rollback to this point if any problems occur `vmadm stop -F $VM_ID && zfs rollback zones/${VM_ID}-disk0@pre${V} && vmadm start $VM_ID`
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
    cd /var/tmp/images && \
    zfs destroy zones/${VM_ID}-disk0@presysprep; \
    zfs snapshot zones/${VM_ID}-disk0@presysprep && \
    zfs send zones/${VM_ID}-disk0@presysprep > "windows-server-${OS_VERSION}-${HYPERVISOR}-${V}.zvol" && \
    vmadm start $VM_ID
    ```

11. Once the VM starts back up, RDP back in and execute `C:\Windows\System32\sysprep\sysprep /generalize /oobe /shutdown /unattend:c:\smartdc\lib\unattend.xml`
12. sysprep.bat will stop the VM, once the VM is stopped, continue to the next step. `vmadm list uuid=$VM_ID -Ho state`
13. Export sysprep version, and rollback to pre-sysprep snapshot, ~30 minutes

    ```bash
    zfs destroy zones/$VM_ID-disk0@sysprep; \
    zfs snapshot zones/$VM_ID-disk0@sysprep && \
    zfs send zones/$VM_ID-disk0@sysprep | gzip -c > /var/tmp/images/windows-server-${OS_VERSION}-${HYPERVISOR}-${V}-sysprep.zvol.gz && \
    zfs destroy zones/$VM_ID-disk0@sysprep; \
    zfs rollback zones/$VM_ID-disk0@presysprep && \
    zfs destroy zones/$VM_ID-disk0@presysprep
    ```

14. Create image manifest

    ```bash
    echo "{
      \"v\": \"2\",
      \"uuid\": \"${IMAGE_UUID}\",
      \"owner\": \"${ADMIN_UUID}\",
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
        \"networks\": [
          {
            \"name\": \"net0\",
            \"description\": \"public\"
          }
        ],
        \"ssh_key\": false
      },
      \"generate_passwords\": \"true\",
      \"users\": [
        {
          \"name\": \"administrator\"
        }
      ],
      \"image_size\": \"61440\",
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

16. Destroy safety net snapshot, `zfs destroy zones/${VM_ID}-disk0@pre${V}`

## Resource notes

* <https://download.joyent.com/pub/vmtools/>
* <https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/latest-virtio/>
* <https://docs.joyent.com/private-cloud/images/kvm>
* <https://docs.joyent.com/private-cloud/images/kvm/windows>

Looks like the following links were replaced by [Joyent doc](https://docs.joyent.com/private-cloud/images) which is missing Windows KVM information, but [SmartOS wiki](https://wiki.smartos.org/display/DOC/How+to+create+a+Virtual+Machine+in+SmartOS) filled most of the gaps for me.

* <https://docs.joyent.com/sdc7/working-with-images/how-to-create-a-kvm-image>
* <https://docs.joyent.com/sdc7/working-with-images/how-to-create-a-kvm-image/how-to-create-a-windows-image>

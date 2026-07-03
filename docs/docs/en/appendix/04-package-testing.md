# Package Testing with Linux and Windows VMs

This guide describes how to test the dcc release packages from an x64 host
running an Ubuntu-based distribution. Use native x64 VMs for the x64 packages
and emulated ARM64 VMs for the ARM64 packages.

The examples use Ubuntu Server 24.04 guests because Ubuntu-based distributions
use `.deb` packages. Windows package testing uses Windows 11 x64 and Windows 11
Arm-based PC guests.

Apple Silicon packages need to be tested on Apple Silicon hardware, such as a
MacBook with an Apple Silicon processor. If you need to isolate the test in a
guest OS, use a MacBook with Parallels installed and run an Apple Silicon macOS
virtual machine.

## Release Downloads

Release page:

```text
https://github.com/gloveboxes/dcc/releases/tag/v2.0.0
```

Direct package URLs:

```text
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-linux-x64.deb
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-linux-arm64.deb
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-linux-x64.tar.gz
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-linux-arm64.tar.gz
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-windows-x64.msi
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-windows-arm64.msi
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-windows-x64.zip
https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-windows-arm64.zip
```

Ubuntu ISO URLs:

```text
https://releases.ubuntu.com/24.04/ubuntu-24.04.4-live-server-amd64.iso
https://cdimage.ubuntu.com/ubuntu/releases/24.04/release/ubuntu-24.04.4-live-server-arm64.iso
```

If a newer Ubuntu point release is available, use the matching ISO from:

```text
https://releases.ubuntu.com/24.04/
https://cdimage.ubuntu.com/ubuntu/releases/24.04/release/
```

The `/24.04/` directories move forward as new Noble point releases are
published. If a pinned ISO URL returns 404, open the directory URL above and use
the newest `ubuntu-24.04.x-live-server-*.iso` file listed there.

Windows ISO download pages:

```text
https://www.microsoft.com/software-download/windows11
https://www.microsoft.com/software-download/windows11arm64
```

Microsoft generates temporary ISO download links from those pages. Download the
x64 ISO for the Windows x64 package and the Arm-based PC ISO for the Windows
ARM64 package.

## Package PATH Behavior

- Linux `.deb`: installs to `/opt/dcc-cpm-z80` and creates command links in
  `/usr/bin` for `dcc`, `dccpeep`, `dccrtlstrip`, `ntvcm`, and `dcc-ma`.
- macOS `.pkg`: installs to `/usr/local/dcc-cpm-z80` and creates command
  links in `/usr/local/bin`.
- Windows `.msi`: rebuilt MSI packages add `%ProgramFiles%\dcc-cpm-z80\bin`
  and `%ProgramFiles%\dcc-cpm-z80\scripts` to the user `Path`. Restart the
  terminal after install so it sees the updated environment.
- Windows `.zip`: `powershell.exe -ExecutionPolicy Bypass -File .\install.ps1 -AddToUserPath` adds the package `bin`
  and `scripts` directories to the user `Path`.

The `v2.0.0` Windows MSI assets add the package directories to the user
`Path`. If the current PowerShell session was already open before installation,
prepend the paths before testing or open a new terminal:

```powershell
$InstallRoot = Join-Path $env:ProgramFiles "dcc-cpm-z80"
$env:Path = "$InstallRoot\bin;$InstallRoot\scripts;$env:Path"
```

## Host Setup on Ubuntu-Based Distributions

Install virtualization tools:

```sh
sudo apt update
sudo apt install -y \
  qemu-kvm \
  qemu-system-arm \
  qemu-efi-aarch64 \
  libvirt-daemon-system \
  libvirt-clients \
  bridge-utils \
  virt-manager \
  virtinst
```

Add your user to the VM groups:

```sh
sudo usermod -aG libvirt,kvm "$USER"
```

If running from PowerShell, use this instead because `$USER` is a shell variable,
not a PowerShell environment variable:

```powershell
sudo usermod -aG libvirt,kvm $env:USER
```

Log out and back in, then verify group membership:

```sh
groups
virsh list --all
```

You should see `libvirt` and `kvm` in `groups`.

Libvirt system VMs run as the `libvirt-qemu` user, which usually cannot traverse
your home directory. For `virt-install` guests, stage ISO files and VM disks
under `/var/lib/libvirt/images` instead of `$HOME` to avoid permission failures.

## Resetting Guests with Snapshots

Install each guest operating system once, apply guest OS updates and basic tools,
then take a clean snapshot before installing any dcc package. Revert to that
clean state before each package test so install, upgrade, and uninstall checks
are repeatable.

For guests managed by libvirt, such as the Linux x64 and Windows x64 examples,
shut the guest down and create a snapshot:

```sh
virsh shutdown dcc-x64
virsh snapshot-create-as dcc-x64 clean-install "Clean OS install before dcc package testing" --atomic
virsh snapshot-list dcc-x64
```

For the Windows x64 guest, use the Windows domain name:

```sh
virsh shutdown dcc-windows-x64
virsh snapshot-create-as dcc-windows-x64 clean-install "Clean OS install before dcc package testing" --atomic
virsh snapshot-list dcc-windows-x64
```

To reset a libvirt guest to the clean state:

```sh
virsh shutdown dcc-x64
virsh snapshot-revert dcc-x64 clean-install
virsh start dcc-x64
```

Replace `dcc-x64` with `dcc-windows-x64` for the Windows x64 guest. If libvirt
rejects a snapshot because of firmware or disk configuration, use the
`virt-manager` Snapshots view for the same guest, or clone the qcow2 disk before
package installation and copy it back when you need to reset.

For direct QEMU guests, such as the Linux ARM64 and Windows ARM64 examples, use
a clean base qcow2 image plus a disposable overlay. After the Linux ARM64 guest
is installed and shut down, convert the installed disk into a base image and
create a writable overlay using the original filename:

```sh
cd ~/VMs/dcc-arm64
mv ubuntu-arm64.qcow2 ubuntu-arm64-base.qcow2
qemu-img create -f qcow2 -F qcow2 -b ubuntu-arm64-base.qcow2 ubuntu-arm64.qcow2
```

To reset the Linux ARM64 guest, delete and recreate only the overlay:

```sh
cd ~/VMs/dcc-arm64
rm -f ubuntu-arm64.qcow2
qemu-img create -f qcow2 -F qcow2 -b ubuntu-arm64-base.qcow2 ubuntu-arm64.qcow2
```

The existing QEMU boot commands keep working because they still point at
`ubuntu-arm64.qcow2`, which is now the disposable overlay.

For Windows ARM64, preserve the clean disk, UEFI variables file, and TPM state
after installation:

```sh
cd ~/VMs/dcc-windows-arm64
cp AAVMF_VARS.fd AAVMF_VARS.clean.fd
cp -a tpm tpm.clean
mv windows-arm64.qcow2 windows-arm64-base.qcow2
qemu-img create -f qcow2 -F qcow2 -b windows-arm64-base.qcow2 windows-arm64.qcow2
```

To reset the Windows ARM64 guest:

```sh
cd ~/VMs/dcc-windows-arm64
rm -rf windows-arm64.qcow2 AAVMF_VARS.fd tpm
cp AAVMF_VARS.clean.fd AAVMF_VARS.fd
cp -a tpm.clean tpm
qemu-img create -f qcow2 -F qcow2 -b windows-arm64-base.qcow2 windows-arm64.qcow2
```

For Apple Silicon package tests in Parallels, use Parallels snapshots. Take a
snapshot after the clean macOS guest setup and before installing the dcc package,
then revert that snapshot before each package test run.

## x64 Guest VM

Download the x64 ISO:

```sh
mkdir -p ~/Downloads/isos ~/VMs/dcc-x64
cd ~/Downloads/isos
wget https://releases.ubuntu.com/24.04/ubuntu-24.04.4-live-server-amd64.iso
```

Create the VM with `virt-install`:

```sh
sudo cp -n ~/Downloads/isos/ubuntu-24.04.4-live-server-amd64.iso /var/lib/libvirt/images/
sudo chmod 0644 /var/lib/libvirt/images/ubuntu-24.04.4-live-server-amd64.iso
virt-install \
  --name dcc-x64 \
  --memory 4096 \
  --vcpus 12 \
  --cpu host \
  --disk path=/var/lib/libvirt/images/dcc-x64.qcow2,size=30,bus=virtio,format=qcow2 \
  --cdrom /var/lib/libvirt/images/ubuntu-24.04.4-live-server-amd64.iso \
  --os-variant ubuntu24.04 \
  --network network=default,model=virtio \
  --graphics spice \
  --boot uefi
```

Finish the Ubuntu installer. After the first clean boot, install SSH in the
guest if it was not selected during install:

```sh
sudo apt update
sudo apt install -y openssh-server
```

Download the x64 package inside the guest:

```sh
wget https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-linux-x64.deb
```

The Linux package uses native binaries and a native `scripts/ma.sh` build
driver, so package smoke tests do not require PowerShell.

Install the package:

```sh
sudo apt install -y ./dcc-cpm-z80-v2.0.0-linux-x64.deb
```

Verify the installed commands:

```sh
uname -m
dcc --version
ntvcm -V
command -v dcc dccpeep dccrtlstrip ntvcm dcc-ma
```

`uname -m` should print `x86_64`.

## ARM64 Guest VM on an x64 Host

An ARM64 guest on an x64 Ubuntu-based host uses QEMU full-system emulation. It
is much slower than the x64 VM, but it is a real ARM64 operating system and is
good for testing the `linux-arm64` package.

Download the ARM64 ISO:

```sh
mkdir -p ~/Downloads/isos ~/VMs/dcc-arm64
cd ~/Downloads/isos
wget https://cdimage.ubuntu.com/ubuntu/releases/24.04/release/ubuntu-24.04.4-live-server-arm64.iso
```

Create a disk:

```sh
mkdir -p ~/VMs/dcc-arm64
qemu-img create -f qcow2 ~/VMs/dcc-arm64/ubuntu-arm64.qcow2 30G
```

Boot the installer:

```sh
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -smp 12 \
  -m 4096 \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -drive if=virtio,file=$HOME/VMs/dcc-arm64/ubuntu-arm64.qcow2,format=qcow2 \
  -cdrom $HOME/Downloads/isos/ubuntu-24.04.4-live-server-arm64.iso \
  -boot d \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device qemu-xhci \
  -device usb-kbd \
  -device usb-tablet \
  -device virtio-gpu-pci \
  -display gtk
```

After installing Ubuntu, shut down and boot from disk:

```sh
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -smp 12 \
  -m 4096 \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -drive if=virtio,file=$HOME/VMs/dcc-arm64/ubuntu-arm64.qcow2,format=qcow2 \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device qemu-xhci \
  -device usb-kbd \
  -device usb-tablet \
  -device virtio-gpu-pci \
  -display gtk
```

If the installer window does not accept keystrokes, click inside the QEMU GTK
window, then press `Ctrl+Alt+G` to toggle the keyboard grab. If it still ignores
input, restart QEMU with the explicit USB keyboard and tablet devices shown
above.

If `cortex-a72` is too slow or does not boot cleanly, try:

```text
-cpu max
```

Inside the ARM64 guest, install SSH if needed:

```sh
sudo apt update
sudo apt install -y openssh-server
```

From the host, copy the ARM64 package into the guest. Replace `ubuntu` with the
user you created during install:

```sh
scp -P 2222 dcc-cpm-z80-v2.0.0-linux-arm64.deb ubuntu@127.0.0.1:/tmp/
```

Or download it directly inside the guest:

```sh
wget https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-linux-arm64.deb
```

The ARM64 Linux package uses native binaries and a native `scripts/ma.sh` build
driver, so package smoke tests do not require PowerShell.

Install the ARM64 package:

```sh
sudo apt install -y ./dcc-cpm-z80-v2.0.0-linux-arm64.deb
```

Verify architecture and installed commands:

```sh
uname -m
dcc --version
ntvcm -V
command -v dcc dccpeep dccrtlstrip ntvcm dcc-ma
```

`uname -m` should print `aarch64`.

## Windows x64 Guest VM

Download the Windows 11 x64 ISO from:

```text
https://www.microsoft.com/software-download/windows11
```

Save it under `~/Downloads/isos`. The generated filename changes over time; the
commands below use `Win11_x64.iso` as a local rename.

```sh
mkdir -p ~/Downloads/isos ~/VMs/dcc-windows-x64
mv ~/Downloads/isos/Win11*_x64*.iso ~/Downloads/isos/Win11_x64.iso
```

Create the VM with UEFI and TPM 2.0 enabled:

```sh
sudo cp -n ~/Downloads/isos/Win11_x64.iso /var/lib/libvirt/images/
sudo chmod 0644 /var/lib/libvirt/images/Win11_x64.iso
virt-install \
  --name dcc-windows-x64 \
  --memory 8192 \
  --vcpus 12 \
  --cpu host \
  --disk path=/var/lib/libvirt/images/dcc-windows-x64.qcow2,size=80,bus=sata,format=qcow2 \
  --cdrom /var/lib/libvirt/images/Win11_x64.iso \
  --os-variant win11 \
  --network network=default,model=e1000e \
  --graphics spice \
  --boot uefi \
  --tpm backend.type=emulator,backend.version=2.0,model=tpm-crb
```

Download and install the x64 MSI:

```powershell
Invoke-WebRequest `
  -Uri "https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-windows-x64.msi" `
  -OutFile "$env:TEMP\dcc-cpm-z80-v2.0.0-windows-x64.msi"

msiexec /i "$env:TEMP\dcc-cpm-z80-v2.0.0-windows-x64.msi" /qn /norestart
```

If the current terminal was already open before MSI installation, update the
current session PATH before testing:

```powershell
$InstallRoot = Join-Path $env:ProgramFiles "dcc-cpm-z80"
$env:Path = "$InstallRoot\bin;$InstallRoot\scripts;$env:Path"
```

Verify the installed commands:

```powershell
$env:PROCESSOR_ARCHITECTURE
dcc --version
ntvcm -V
Get-Command dcc,dccpeep,dccrtlstrip,ntvcm,ma.ps1
```

`$env:PROCESSOR_ARCHITECTURE` should print `AMD64`.

## Windows ARM64 Guest VM on an x64 Host

Windows ARM64 on an x64 Linux host uses QEMU full-system emulation. Expect it to
be much slower than the Windows x64 VM.

Download the Windows 11 Arm-based PC ISO from:

```text
https://www.microsoft.com/software-download/windows11arm64
```

Save it under `~/Downloads/isos`. The generated filename changes over time; the
commands below use `Win11_ARM64.iso` as a local rename.

```sh
mkdir -p ~/Downloads/isos ~/VMs/dcc-windows-arm64
mv ~/Downloads/isos/Win11*_ARM64*.iso ~/Downloads/isos/Win11_ARM64.iso
cp /usr/share/AAVMF/AAVMF_VARS.ms.fd ~/VMs/dcc-windows-arm64/AAVMF_VARS.fd
qemu-img create -f qcow2 ~/VMs/dcc-windows-arm64/windows-arm64.qcow2 80G
```

Windows 11 requires TPM 2.0 and Secure Boot. The `AAVMF_CODE.secboot.fd` firmware
and `AAVMF_VARS.ms.fd` variables file provide Secure Boot with Microsoft keys.
Start a software TPM before booting the installer:

```sh
mkdir -p ~/VMs/dcc-windows-arm64/tpm
rm -f ~/VMs/dcc-windows-arm64/swtpm-sock
swtpm socket \
  --tpm2 \
  --tpmstate dir=$HOME/VMs/dcc-windows-arm64/tpm \
  --ctrl type=unixio,path=$HOME/VMs/dcc-windows-arm64/swtpm-sock \
  --daemon
```

If you already booted with `AAVMF_VARS.fd` and saw the Windows requirements
screen, shut down QEMU and replace the variables file before retrying:

```sh
cp /usr/share/AAVMF/AAVMF_VARS.ms.fd ~/VMs/dcc-windows-arm64/AAVMF_VARS.fd
```

Boot the installer:

```sh
qemu-system-aarch64 \
  -machine virt \
  -cpu max \
  -smp 12 \
  -m 8192 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.secboot.fd \
  -drive if=pflash,format=raw,file=$HOME/VMs/dcc-windows-arm64/AAVMF_VARS.fd \
  -chardev socket,id=chrtpm,path=$HOME/VMs/dcc-windows-arm64/swtpm-sock \
  -tpmdev emulator,id=tpm0,chardev=chrtpm \
  -device tpm-tis-device,tpmdev=tpm0 \
  -drive if=none,id=system,file=$HOME/VMs/dcc-windows-arm64/windows-arm64.qcow2,format=qcow2 \
  -device nvme,drive=system,serial=dccwinarm64 \
  -drive if=none,id=install,media=cdrom,readonly=on,file=$HOME/Downloads/isos/Win11_ARM64.iso \
  -device qemu-xhci \
  -device usb-storage,drive=install \
  -device usb-kbd \
  -device usb-tablet \
  -device ramfb \
  -display gtk \
  -netdev user,id=net0,hostfwd=tcp::3390-:3389 \
  -device e1000e,netdev=net0
```

After installation, boot from disk with the same command but omit the `install`
drive and `usb-storage` lines. Start `swtpm` first if it is not already running.

Inside the Windows ARM64 guest, install the ARM64 MSI:

```powershell
Invoke-WebRequest `
  -Uri "https://github.com/gloveboxes/dcc/releases/download/v2.0.0/dcc-cpm-z80-v2.0.0-windows-arm64.msi" `
  -OutFile "$env:TEMP\dcc-cpm-z80-v2.0.0-windows-arm64.msi"

msiexec /i "$env:TEMP\dcc-cpm-z80-v2.0.0-windows-arm64.msi" /qn /norestart
```

If the current terminal was already open before MSI installation, update the
current session PATH before testing:

```powershell
$InstallRoot = Join-Path $env:ProgramFiles "dcc-cpm-z80"
$env:Path = "$InstallRoot\bin;$InstallRoot\scripts;$env:Path"
```

Verify the installed commands:

```powershell
$env:PROCESSOR_ARCHITECTURE
dcc --version
ntvcm -V
Get-Command dcc,dccpeep,dccrtlstrip,ntvcm,ma.ps1
```

`$env:PROCESSOR_ARCHITECTURE` should print `ARM64`.

## Linux Guest Smoke Test

Create a small C program:

```sh
mkdir -p ~/dcc-smoke
cd ~/dcc-smoke
cat > hello.c <<'EOF'
#include <stdio.h>

int main(void) {
    printf("hello from dcc package\n");
    return 0;
}
EOF
```

Build it with the installed helper:

```sh
dcc-ma hello --source-path ./hello.c --mode fast
```

Run the generated CP/M program:

```sh
ntvcm build/HELLO.COM
```

Expected output:

```text
hello from dcc package
```

Also verify the package files are where the installer expects them:

```sh
ls -l /opt/dcc-cpm-z80
ls -l /opt/dcc-cpm-z80/bin
ls -l /opt/dcc-cpm-z80/scripts/ma.sh /opt/dcc-cpm-z80/scripts/ma.ps1
ls -l /opt/dcc-cpm-z80/dcc-env.sh
ls -l /opt/dcc-cpm-z80/DCCRTL.MAC /opt/dcc-cpm-z80/m80.com /opt/dcc-cpm-z80/l80.com
```

## Linux Uninstall Test

Remove the package:

```sh
sudo apt remove -y dcc-cpm-z80
```

Confirm commands are gone:

```sh
command -v dcc || true
command -v ntvcm || true
command -v dcc-ma || true
```

## Windows Guest Smoke Test

Open Windows PowerShell in the Windows guest. If the terminal was already open before
MSI installation, prepend the installed package paths for the current session
first:

```powershell
$InstallRoot = Join-Path $env:ProgramFiles "dcc-cpm-z80"
$env:Path = "$InstallRoot\bin;$InstallRoot\scripts;$env:Path"
```

Create a small C program:

```powershell
New-Item -ItemType Directory -Force -Path "$HOME\dcc-smoke" | Out-Null
Set-Location "$HOME\dcc-smoke"

@'
#include <stdio.h>

int main(void) {
  printf("hello from dcc package\n");
  return 0;
}
'@ | Set-Content -LiteralPath .\hello.c -Encoding ascii
```

Build it with the installed helper script:

```powershell
powershell.exe -ExecutionPolicy Bypass -File "$InstallRoot\scripts\ma.ps1" hello -SourcePath .\hello.c -Mode fast
```

Run the generated CP/M program:

```powershell
ntvcm .\build\HELLO.COM
```

Expected output:

```text
hello from dcc package
```

Also verify the package files are where the installer expects them:

```powershell
Get-ChildItem "$InstallRoot"
Get-ChildItem "$InstallRoot\bin"
Test-Path "$InstallRoot\scripts\ma.ps1"
Test-Path "$InstallRoot\DCCRTL.MAC"
Test-Path "$InstallRoot\m80.com"
Test-Path "$InstallRoot\l80.com"
```

## Windows Uninstall Test

Uninstall using the same MSI file you installed. Use the x64 filename in the x64
guest and the ARM64 filename in the ARM64 guest:

```powershell
msiexec /x "$env:TEMP\dcc-cpm-z80-v2.0.0-windows-x64.msi" /qn /norestart
```

Confirm the commands are gone:

```powershell
Get-Command dcc -ErrorAction SilentlyContinue
Get-Command ntvcm -ErrorAction SilentlyContinue
Test-Path "$env:ProgramFiles\dcc-cpm-z80"
```

## Notes

- The ARM64 VM is expected to be slow on an x64 host.
- The `.deb` package installs under `/opt/dcc-cpm-z80` and places command
  links in `/usr/bin`.
- The Windows MSI installs under `%ProgramFiles%\dcc-cpm-z80`.
- On Linux and macOS, `dcc-ma` wraps the native `scripts/ma.sh` helper.
- On Windows, `ma.ps1` works with the Windows PowerShell 5.1 already included
  with Windows, or with PowerShell 7+ if it is installed.
- The portable `.tar.gz` packages can be tested without root by extracting them
  and running `./install.sh` with custom `PREFIX` and `LINK_DIR` values.

# sshtars (vendored)

`ssh.tar.gz` from https://github.com/verygenericname/SSHRD_Script
(submodule verygenericname/sshtars @ 6c05c8a7, vendored 2026-11-01).
License: BSD-3-Clause (see LICENSE-SSHRD_Script, upstream LICENSE).

Contents: dropbear (+ blank-password auth, verified via strings) with
host keys in private/etc/dropbear, bash/sh, launchctl, core shell
tools, img4tool/ldid, wget/dbclient/ssh, sftp-server, and
`usr/local/bin/restored_external` (SSHRD_Script Sep 2022 build) which
execl's `/usr/local/bin/dropbear` over the USB mux and runs
`/bin/bash /usr/bin/mount_filesystems`.

Role in tr4mpass: unpacked into the (already decrypted, E31) stock
Erase ramdisk by scripts/build_ssh_rd.sh; boots via the pongo shell
(pongo_load_ramdisk.sh), SSH reaches the device over usbmuxd
(iproxy 2222) as root/alpine.

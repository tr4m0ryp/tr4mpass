#!/usr/bin/expect -f
# ssh_expect22.sh -- run one command over SSH into the booted SSHRD
# (dropbear on localhost:2223 via `iproxy 2223 22`, password alpine).
#
# Usage: ssh_expect22.sh '<remote command>'
# Behaves exactly as the original ~/.cache/tr4mpass copy (kept for the
# same day E55-E68 runs); this repo copy is canonical.
set timeout 20
set cmd [lindex $argv 0]
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 -p 2223 root@localhost $cmd
expect {
    "password:" { send "alpine\r"; exp_continue }
    eof
}

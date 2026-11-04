#!/usr/bin/expect -f
# sftp_expect.sh -- pull one file from the booted SSHRD over sftp
# (localhost port 2223, password alpine). Used for E68-style forensics
# pulls (tracev3 tails, CrashReporter *.ips).
#
# Usage: sftp_expect.sh 'get /path/on/device /dest/or/dash'
set timeout 30
set cmd [lindex $argv 0]
spawn sftp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2223 root@localhost
expect "password:"
send "alpine\r"
expect "sftp>"
send "$cmd\r"
expect {
    "sftp>" {}
    timeout { exit 1 }
}
send "quit\r"
expect eof

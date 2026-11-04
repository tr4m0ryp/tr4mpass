# Useful commands

```sh
## build + tests
make && make test

## run
./tr4mpass            # automatic detection + path_a (checkm8)
./tr4mpass -v         # debug logs

## check the marker on the device in DFU
system_profiler SPUSBDataType | grep -A6 "1227"
ioreg -p IOUSB -l -w 0 | grep -i "USB Serial Number"
```

Success criteria for a run:

```
overwrite STALL confirmed (UAF write landed)
GETSTATUS[0] state=0x06 / [1] 0x07 / [2] 0x08
serial contains PWND:  ->  checkm8_verify_pwned: device processed successfully
```

---

---

[← Previous](open-leads.md) · [Index](README.md) · [Next →](references.md)

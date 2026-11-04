#!/usr/bin/env python3
"""
Pull a single file out of a remote Apple IPSW using HTTP Range requests.

An IPSW is a ZIP archive; the central directory at the end of the file
lists every entry with its local-header offset and compressed size.  So
we can fetch just the bytes we need instead of the whole ~5 GB file.

Usage:
  ipsw_pull.py list   <device-id>
  ipsw_pull.py probe  <ipsw-url> <entry-regex> <needle>
  ipsw_pull.py fetch  <ipsw-url> <entry-regex> <outdir>
"""
import json
import os
import re
import struct
import sys
import urllib.request
import zlib

UA = {"User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)"}


def get(url, start=None, end=None, timeout=60):
    req = urllib.request.Request(url, headers=dict(UA))
    if start is not None:
        req.add_header("Range", "bytes=%d-%d" % (start, end))
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def content_length(url):
    req = urllib.request.Request(url, headers=dict(UA), method="HEAD")
    with urllib.request.urlopen(req, timeout=30) as r:
        return int(r.headers["Content-Length"])


EOCD_SIG = b"PK\x05\x06"
EOCD64_LOC_SIG = b"PK\x06\x07"
EOCD64_SIG = b"PK\x06\x06"


def read_central_directory(url, total=None):
    """Return [(name, method, comp_size, local_offset)]."""
    if total is None:
        total = content_length(url)

    tail_len = min(total, 66560)
    tail = get(url, total - tail_len, total - 1)
    pos = tail.rfind(EOCD_SIG)
    if pos < 0:
        raise RuntimeError("EOCD not found (not a zip?)")

    (_, _, _, _, n_entries, cd_size, cd_offset, _) = struct.unpack(
        "<4sHHHHIIH", tail[pos:pos + 22])

    # ZIP64: if values are saturated, follow the ZIP64 EOCD record.
    if cd_offset == 0xFFFFFFFF or n_entries == 0xFFFF:
        loc = tail.rfind(EOCD64_LOC_SIG, 0, pos)
        if loc < 0:
            raise RuntimeError("zip64 locator not found")
        eocd64_off = struct.unpack("<Q", tail[loc + 8:loc + 16])[0]
        rec = get(url, eocd64_off, eocd64_off + 55)
        (_, _, _, _, _, _, _, n_entries, cd_size, cd_offset) = struct.unpack(
            "<4sQHHIIQQQQ", rec[:56])

    cd = get(url, cd_offset, cd_offset + cd_size - 1)

    entries = []
    i = 0
    while i + 46 <= len(cd) and cd[i:i + 4] == b"PK\x01\x02":
        method = struct.unpack("<H", cd[i + 10:i + 12])[0]
        comp_size = struct.unpack("<I", cd[i + 20:i + 24])[0]
        uncomp_size = struct.unpack("<I", cd[i + 24:i + 28])[0]
        name_len = struct.unpack("<H", cd[i + 28:i + 30])[0]
        extra_len = struct.unpack("<H", cd[i + 30:i + 32])[0]
        cmt_len = struct.unpack("<H", cd[i + 32:i + 34])[0]
        local_off = struct.unpack("<I", cd[i + 42:i + 46])[0]
        name = cd[i + 46:i + 46 + name_len].decode("utf-8", "replace")

        if 0xFFFFFFFF in (uncomp_size, comp_size, local_off):
            # zip64 extra field (id 0x0001): the 64-bit values appear in
            # CANONICAL order -- uncompressed size, compressed size,
            # header offset -- but ONLY for the plain fields that were
            # saturated.  Apple IPSWs saturate all of them even when the
            # value fits in 32 bits, so we must count quads in order.
            # (2026-11-01: previous code assumed only csize+offset could
            # be saturated and mis-assigned fields on an iOS 15.x build.)
            ex = cd[i + 46 + name_len:i + 46 + name_len + extra_len]
            j = 0
            while j + 4 <= len(ex):
                hid, hsz = struct.unpack("<HH", ex[j:j + 4])
                if hid == 0x0001:
                    k = j + 4
                    if uncomp_size == 0xFFFFFFFF and k + 8 <= j + 4 + hsz:
                        uncomp_size = struct.unpack("<Q", ex[k:k + 8])[0]
                        k += 8
                    if comp_size == 0xFFFFFFFF and k + 8 <= j + 4 + hsz:
                        comp_size = struct.unpack("<Q", ex[k:k + 8])[0]
                        k += 8
                    if local_off == 0xFFFFFFFF and k + 8 <= j + 4 + hsz:
                        local_off = struct.unpack("<Q", ex[k:k + 8])[0]
                        k += 8
                j += 4 + hsz

        entries.append((name, method, comp_size, local_off))
        i += 46 + name_len + extra_len + cmt_len

    return entries


def extract(url, entry, outdir):
    name, method, comp_size, local_off = entry
    lh = get(url, local_off, local_off + 29)
    if lh[:4] != b"PK\x03\x04":
        raise RuntimeError("bad local header for %s" % name)
    name_len = struct.unpack("<H", lh[26:28])[0]
    extra_len = struct.unpack("<H", lh[28:30])[0]
    data_off = local_off + 30 + name_len + extra_len

    blob = get(url, data_off, data_off + comp_size - 1)
    if method == 8:
        blob = zlib.decompress(blob, -15)
    elif method != 0:
        raise RuntimeError("unsupported method %d" % method)

    base = name.rsplit("/", 1)[-1]
    path = "%s/%s" % (outdir, base)
    with open(path, "wb") as f:
        f.write(blob)
    return path, len(blob)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd = sys.argv[1]

    if cmd == "list":
        dev = sys.argv[2]
        data = json.loads(get("https://api.ipsw.me/v4/device/%s?type=ipsw" % dev))
        print("device:", data.get("name"))
        for f in data["firmwares"]:
            print("%-10s %-12s %s  %.0f MB  signed=%s  %s" % (
                f["version"], f["buildid"], f["releasedate"][:10],
                f["filesize"] / 1e6, f["signed"], f["url"]))
        return 0

    if cmd == "url-for":
        # Resolve an exact build (e.g. 13A342) to its IPSW download URL.
        dev = sys.argv[2]
        build = sys.argv[3]
        data = json.loads(get("https://api.ipsw.me/v4/device/%s?type=ipsw" % dev))
        for f in data["firmwares"]:
            if f["buildid"] == build:
                print(f["url"])
                return 0
        print("ERROR: build %s not found for %s" % (build, dev), file=sys.stderr)
        return 1

    url = sys.argv[2]
    pattern = sys.argv[3]
    rx = re.compile(pattern)

    entries = read_central_directory(url)
    matches = [e for e in entries if rx.search(e[0])]
    if not matches:
        print("no match for %r" % pattern)
        print("candidates:")
        for e in entries:
            if "dfu" in e[0].lower() or "iBSS" in e[0] or "iBEC" in e[0]:
                print("  ", e[0])
        return 1

    if cmd == "probe":
        needle = sys.argv[4].encode()
        needle_l = needle.lower()
        # Rule: state under the tr4mpass state dir, never /tmp.
        probe_dir = os.path.join(
            os.environ.get("TR4MPASS_STATE_DIR",
                           os.path.expanduser("~/.cache/tr4mpass")),
            "images")
        os.makedirs(probe_dir, exist_ok=True)
        for e in matches:
            path, size = extract(url, e, probe_dir)
            blob = open(path, "rb").read()
            hit = needle_l in blob.lower()
            print("%-45s %7d bytes  needle=%s" % (e[0].rsplit("/", 1)[-1], size,
                                                  "FOUND" if hit else "-"))
            if hit:
                for m in re.finditer(rb"iBoot-[0-9.]+", blob):
                    print("    version string:", m.group().decode())
        return 0

    if cmd == "fetch":
        outdir = sys.argv[4]
        for e in matches:
            path, size = extract(url, e, outdir)
            print("wrote %s (%d bytes)" % (path, size))
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())

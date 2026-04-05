#!/usr/bin/env python3
"""
Safe /etc/hosts modification for activation server redirection.

Adds and removes redirect entries using a unique marker so only our
entries are affected. Registers atexit and signal handlers to guarantee
cleanup even on crash or SIGTERM.
"""

import os
import sys
import atexit
import signal
import logging

logger = logging.getLogger("hosts_manager")

HOSTS_PATH = "/etc/hosts"
MARKER = "# tr4mpass-bypass"

# All Apple activation-related domains we need to intercept
ENTRIES = {
    "albert.apple.com": "127.0.0.1",
    "humb.apple.com": "127.0.0.1",
    "gs.apple.com": "127.0.0.1",
    "static.ips.apple.com": "127.0.0.1",
    "captive.apple.com": "127.0.0.1",
    "mesu.apple.com": "127.0.0.1",
    "identity.ess.apple.com": "127.0.0.1",
    "setup.icloud.com": "127.0.0.1",
}

_cleanup_registered = False


def _build_lines():
    """Build the lines to append to /etc/hosts."""
    lines = [f"\n{MARKER} -- BEGIN"]
    for domain, ip in ENTRIES.items():
        lines.append(f"{ip} {domain} {MARKER}")
    lines.append(f"{MARKER} -- END\n")
    return "\n".join(lines)


def is_active():
    """Check if our redirect entries are currently in /etc/hosts."""
    try:
        with open(HOSTS_PATH, "r") as f:
            return MARKER in f.read()
    except (IOError, PermissionError):
        return False


def add_entries():
    """Add redirect entries to /etc/hosts with marker."""
    if is_active():
        logger.info("Hosts entries already present, skipping add")
        return True

    try:
        with open(HOSTS_PATH, "r") as f:
            original = f.read()
    except (IOError, PermissionError) as e:
        logger.error("Cannot read %s: %s", HOSTS_PATH, e)
        return False

    new_content = original.rstrip("\n") + "\n" + _build_lines()

    try:
        with open(HOSTS_PATH, "w") as f:
            f.write(new_content)
    except PermissionError:
        logger.error("Cannot write %s -- run with sudo", HOSTS_PATH)
        return False

    _register_cleanup()
    logger.info("Added %d redirect entries to %s", len(ENTRIES), HOSTS_PATH)
    return True


def remove_entries():
    """Remove only our marked entries from /etc/hosts."""
    if not is_active():
        logger.info("No tr4mpass entries found in hosts, nothing to remove")
        return True

    try:
        with open(HOSTS_PATH, "r") as f:
            lines = f.readlines()
    except (IOError, PermissionError) as e:
        logger.error("Cannot read %s: %s", HOSTS_PATH, e)
        return False

    cleaned = [line for line in lines if MARKER not in line]

    # Remove trailing blank lines that we may have added
    while cleaned and cleaned[-1].strip() == "":
        cleaned.pop()
    cleaned.append("\n")

    try:
        with open(HOSTS_PATH, "w") as f:
            f.writelines(cleaned)
    except PermissionError:
        logger.error("Cannot write %s -- run with sudo", HOSTS_PATH)
        return False

    logger.info("Removed tr4mpass entries from %s", HOSTS_PATH)
    return True


def _cleanup_on_exit():
    """atexit handler -- always remove entries."""
    remove_entries()


def _signal_handler(signum, frame):
    """Signal handler -- clean up and exit."""
    logger.info("Caught signal %d, cleaning up hosts file", signum)
    remove_entries()
    sys.exit(128 + signum)


def _register_cleanup():
    """Register cleanup handlers exactly once."""
    global _cleanup_registered
    if _cleanup_registered:
        return
    atexit.register(_cleanup_on_exit)
    signal.signal(signal.SIGTERM, _signal_handler)
    signal.signal(signal.SIGINT, _signal_handler)
    _cleanup_registered = True
    logger.info("Registered cleanup handlers for hosts file")


def print_status():
    """Print current status of hosts redirects."""
    if is_active():
        print(f"[ACTIVE] tr4mpass entries present in {HOSTS_PATH}")
        print("Redirected domains:")
        for domain, ip in ENTRIES.items():
            print(f"  {ip} -> {domain}")
    else:
        print(f"[INACTIVE] No tr4mpass entries in {HOSTS_PATH}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s | %(name)s | %(message)s",
        datefmt="%H:%M:%S",
    )

    if len(sys.argv) < 2:
        print("Usage: sudo python3 hosts_manager.py [add|remove|status]")
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd == "add":
        ok = add_entries()
        sys.exit(0 if ok else 1)
    elif cmd == "remove":
        ok = remove_entries()
        sys.exit(0 if ok else 1)
    elif cmd == "status":
        print_status()
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)

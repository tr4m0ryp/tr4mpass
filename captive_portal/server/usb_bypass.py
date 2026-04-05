#!/usr/bin/env python3
"""
Automated USB-based iOS activation bypass.

Detects an activation-locked iOS device over USB, gathers device info,
and runs the activation flow against our local fake server.

Activation methods are in usb_activate.py (split for file size limit).

Requires:
  - libimobiledevice tools (idevice_id, ideviceinfo, ideviceactivation)
  - OR pymobiledevice3 as fallback
  - sudo for /etc/hosts modification
  - HTTPS server (portal_https.py) running or started by run_bypass.sh

Zero user interaction -- fully automated.
"""

import sys
import time
import shutil
import subprocess
import logging

logger = logging.getLogger("usb_bypass")

# Import activation methods from split module
from usb_activate import attempt_activation, verify_activation  # noqa: E402

# ---------------------------------------------------------------------------
# Tool detection
# ---------------------------------------------------------------------------
IMOBILEDEVICE_TOOLS = {
    "idevice_id": shutil.which("idevice_id"),
    "ideviceinfo": shutil.which("ideviceinfo"),
    "ideviceactivation": shutil.which("ideviceactivation"),
}

HAS_PYMOBILEDEVICE3 = False
try:
    import pymobiledevice3  # noqa: F401
    HAS_PYMOBILEDEVICE3 = True
except ImportError:
    pass


def _check_tools():
    """Verify that required tools are available."""
    missing = [k for k, v in IMOBILEDEVICE_TOOLS.items() if v is None]
    if missing and not HAS_PYMOBILEDEVICE3:
        logger.error(
            "Missing tools: %s. Install libimobiledevice "
            "(brew install libimobiledevice libideviceactivation) "
            "or pymobiledevice3 (pip install pymobiledevice3).",
            ", ".join(missing),
        )
        return False
    if missing:
        logger.info(
            "libimobiledevice tools not found (%s), using pymobiledevice3",
            ", ".join(missing),
        )
    return True


# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------
def _run(cmd, timeout=15):
    """Run a command and return stdout, or None on failure."""
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout
        )
        if result.returncode == 0:
            return result.stdout.strip()
        logger.debug("Command %s returned %d: %s",
                      cmd, result.returncode, result.stderr.strip())
        return None
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        logger.debug("Command %s failed: %s", cmd, e)
        return None


def _ideviceinfo_key(key, udid=None):
    """Read a single key from ideviceinfo."""
    cmd = ["ideviceinfo", "-k", key]
    if udid:
        cmd += ["-u", udid]
    return _run(cmd)


# ---------------------------------------------------------------------------
# Phase 1: Device detection
# ---------------------------------------------------------------------------
def detect_device(max_wait=60):
    """
    Wait for a USB-connected iOS device and return its UDID.

    Polls every 2 seconds up to max_wait seconds.
    """
    logger.info("Waiting for iOS device over USB...")
    elapsed = 0
    while elapsed < max_wait:
        if IMOBILEDEVICE_TOOLS["idevice_id"]:
            out = _run(["idevice_id", "-l"])
            if out:
                udid = out.splitlines()[0].strip()
                if udid:
                    logger.info("Device detected: %s", udid)
                    return udid

        if HAS_PYMOBILEDEVICE3:
            try:
                from pymobiledevice3.usbmux import list_devices
                devices = list_devices()
                if devices:
                    udid = devices[0].serial
                    logger.info("Device detected (pymobiledevice3): %s", udid)
                    return udid
            except Exception as e:
                logger.debug("pymobiledevice3 detection failed: %s", e)

        time.sleep(2)
        elapsed += 2
        if elapsed % 10 == 0:
            logger.info("  Still waiting... (%ds)", elapsed)

    logger.error("No device detected after %ds", max_wait)
    return None


# ---------------------------------------------------------------------------
# Phase 2: Gather device info
# ---------------------------------------------------------------------------
def gather_device_info(udid):
    """Collect device properties and return as dict."""
    keys = {
        "ProductVersion": "iOS Version",
        "ProductType": "Model",
        "UniqueChipID": "ECID",
        "ChipID": "CPID",
        "SerialNumber": "Serial",
        "InternationalMobileEquipmentIdentity": "IMEI",
        "MobileEquipmentIdentifier": "MEID",
        "ActivationState": "Activation State",
    }

    info = {}
    for key, label in keys.items():
        val = _ideviceinfo_key(key, udid)
        info[key] = val
        if val:
            logger.info("  %-20s : %s", label, val)
        else:
            logger.debug("  %-20s : (not available)", label)

    return info


# ---------------------------------------------------------------------------
# Phase 3: Check activation state
# ---------------------------------------------------------------------------
def check_activation_state(info):
    """
    Evaluate the ActivationState field.

    Returns:
        str: "activated" or "needs_bypass"
    """
    state = (info.get("ActivationState") or "").strip()

    if state == "Activated":
        logger.info("Device is already activated")
        return "activated"
    elif state in ("Unactivated", "FactoryActivated", ""):
        logger.info("Device state: %s -- proceeding with bypass",
                     state or "empty")
        return "needs_bypass"
    else:
        logger.warning("Unknown activation state: %s -- attempting bypass",
                        state)
        return "needs_bypass"


# ---------------------------------------------------------------------------
# Main orchestrator
# ---------------------------------------------------------------------------
def run_bypass():
    """
    Execute the full bypass flow.

    Returns:
        int: 0 on success, 1 on failure.
    """
    print("=" * 56)
    print("  tr4mpass -- USB Activation Bypass")
    print("=" * 56)
    print()

    if not _check_tools():
        return 1

    # Phase 1: detect
    udid = detect_device(max_wait=60)
    if not udid:
        return 1

    # Phase 2: gather info
    print()
    logger.info("--- Device Information ---")
    info = gather_device_info(udid)

    # Phase 3: check state
    print()
    state = check_activation_state(info)
    if state == "activated":
        print()
        print("Device is already activated. Nothing to do.")
        return 0

    # Phase 4: activate (delegated to usb_activate module)
    print()
    logger.info("--- Attempting Activation ---")
    attempt_activation(
        udid,
        ideviceactivation_path=IMOBILEDEVICE_TOOLS["ideviceactivation"],
    )

    # Phase 5: verify
    print()
    logger.info("--- Verifying ---")
    if verify_activation(udid, ideviceinfo_key_fn=_ideviceinfo_key):
        print()
        print("[SUCCESS] Device is now activated.")
        return 0
    else:
        print()
        print("[INCOMPLETE] Device state did not change to Activated.")
        print("Check server logs for activation request/response details.")
        print("The activation record placeholders may need real values.")
        return 1


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s | %(name)-14s | %(message)s",
        datefmt="%H:%M:%S",
    )

    sys.exit(run_bypass())

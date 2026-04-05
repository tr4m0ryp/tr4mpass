#!/usr/bin/env python3
"""
USB activation methods for iOS devices.

Provides activation via two backends:
  1. ideviceactivation (libimobiledevice) -- uses /etc/hosts redirect
  2. pymobiledevice3 -- direct USB activation record push

Split from usb_bypass.py to keep files under 300 lines.
"""

import os
import sys
import time
import subprocess
import logging

logger = logging.getLogger("usb_activate")


# ---------------------------------------------------------------------------
# Method 1: ideviceactivation (libimobiledevice)
# ---------------------------------------------------------------------------
def activate_via_libimobile(udid, tool_path=None):
    """
    Use ideviceactivation to activate.

    Traffic goes to our fake server via /etc/hosts redirect.

    Args:
        udid: Device UDID.
        tool_path: Path to ideviceactivation binary, or None to skip.

    Returns:
        bool: True if activation command succeeded.
    """
    if not tool_path:
        logger.info("ideviceactivation not available, skipping")
        return False

    logger.info("Running: ideviceactivation activate")
    cmd = [tool_path, "activate"]
    if udid:
        cmd += ["-u", udid]

    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30
        )
        logger.info("  stdout: %s", result.stdout.strip())
        if result.stderr.strip():
            logger.info("  stderr: %s", result.stderr.strip())
        return result.returncode == 0
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        logger.error("  ideviceactivation failed: %s", e)
        return False


# ---------------------------------------------------------------------------
# Method 2: pymobiledevice3 direct USB
# ---------------------------------------------------------------------------
def activate_via_pymobiledevice3(udid):
    """
    Use pymobiledevice3 to push an activation record directly over USB.

    This bypasses the /etc/hosts approach entirely by talking to the
    device's MobileActivation service directly via lockdownd.

    Args:
        udid: Device UDID.

    Returns:
        bool: True if activation succeeded.
    """
    try:
        import pymobiledevice3  # noqa: F401
    except ImportError:
        logger.info("pymobiledevice3 not available, skipping")
        return False

    logger.info("Attempting activation via pymobiledevice3")
    try:
        from pymobiledevice3.lockdown import create_using_usbmux
        from pymobiledevice3.services.mobile_activation import (
            MobileActivationService,
        )

        lockdown = create_using_usbmux(serial=udid)
        activation = MobileActivationService(lockdown)

        state = activation.get_activation_state()
        logger.info("  Current state: %s", state)

        if state == "Activated":
            return True

        # Get activation info from device
        logger.info("  Fetching activation info from device...")
        act_info = activation.create_activation_info()
        logger.info("  Got activation info (%d bytes)", len(str(act_info)))

        # Build activation record using our response builder
        script_dir = os.path.dirname(os.path.abspath(__file__))
        sys.path.insert(0, script_dir)
        from activation_responses import build_activation_record
        import plistlib

        record_bytes = build_activation_record(str(act_info))
        record = plistlib.loads(record_bytes)

        logger.info("  Submitting activation record to device...")
        activation.activate(record.get("ActivationRecord", record))
        logger.info("  Activation record submitted")
        return True

    except Exception as e:
        logger.error("  pymobiledevice3 activation failed: %s", e)
        return False


# ---------------------------------------------------------------------------
# Combined dispatcher
# ---------------------------------------------------------------------------
def attempt_activation(udid, ideviceactivation_path=None):
    """
    Try all available activation methods in order.

    Args:
        udid: Device UDID.
        ideviceactivation_path: Path to ideviceactivation binary.

    Returns:
        bool: True if any method succeeded.
    """
    # Method 1: ideviceactivation with /etc/hosts redirect
    if activate_via_libimobile(udid, ideviceactivation_path):
        return True

    # Method 2: pymobiledevice3 direct USB push
    if activate_via_pymobiledevice3(udid):
        return True

    logger.error("All activation methods failed")
    return False


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------
def verify_activation(udid, ideviceinfo_key_fn=None):
    """
    Check if device is now activated.

    Args:
        udid: Device UDID.
        ideviceinfo_key_fn: Callable(key, udid) -> str for querying device.

    Returns:
        bool: True if device reports Activated state.
    """
    time.sleep(2)  # Brief pause for device state to settle

    # Try ideviceinfo first
    if ideviceinfo_key_fn:
        state = ideviceinfo_key_fn("ActivationState", udid)
        logger.info("Post-activation state: %s", state or "(unknown)")
        if state == "Activated":
            return True

    # Fallback: pymobiledevice3
    try:
        from pymobiledevice3.lockdown import create_using_usbmux
        from pymobiledevice3.services.mobile_activation import (
            MobileActivationService,
        )
        lockdown = create_using_usbmux(serial=udid)
        activation = MobileActivationService(lockdown)
        state = activation.get_activation_state()
        logger.info("Post-activation state (pymobiledevice3): %s", state)
        return state == "Activated"
    except ImportError:
        pass
    except Exception as e:
        logger.debug("pymobiledevice3 verify failed: %s", e)

    return False

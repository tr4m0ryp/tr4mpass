#!/bin/bash
#
# run_path_a.sh -- one-command staged runner for the Path A pipeline.
#
# What it does:
#   Validates the TR4MPASS_* environment, then runs ./tr4mpass -v with
#   all output teed to a timestamped log under ~/.cache/tr4mpass/logs/,
#   exits with the pipeline's status, and prints the USB serial line so
#   the PWND/activation state is visible at a glance afterwards.
#
# The 8 pipeline steps and what --stop-after N means per stage
# (docs/execution-plan.md, Phase 4):
#   [1/8] pwn (checkm8)              \
#   [2/8] re-pwn / skip-if-pwned      \  Stage A
#   [3/8] ramdisk upload              /  --stop-after 4
#   [4/8] kernel patches (AMFI...)   /
#   [5/8] replace mobileactivationd  -- Stage B: --stop-after 5
#   [6/8] activation record          \
#   [7/8] deletescript (Setup.app)   /  Stage C: --stop-after 7
#   [8/8] verify activated           -- Stage D: --stop-after 8 (default)
#
# Stage A validates pwn + ramdisk + patches without touching
# mobileactivationd; B/C/D add the activation steps one tier at a
# time. Full reference: docs/execution-plan.md (Phases 4-5).
#
# Honesty note: this is the Path A IPSW route. The device must already
# be in DFU mode when this script starts -- it does not put it there,
# and it does not wait for one.
#
# Required env (always): TR4MPASS_IBSS_PATH, TR4MPASS_IBEC_PATH,
#   TR4MPASS_DEVICETREE_PATH, TR4MPASS_RAMDISK_PATH.
# Required env (stop-after >= 5): TR4MPASS_PATCHED_MAD.
# Optional env: TR4MPASS_TRUSTCACHE_PATH.
#
# Usage: scripts/run_path_a.sh [--stop-after N] [--dry-run]
#                              [-a|--force-path-a] [-b|--force-path-b]
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

fail() { echo "FAIL: $*" >&2; exit 1; }
info() { echo "[run-path-a] $*"; }
usage() { sed -n '2,36p' "$SCRIPT_DIR/run_path_a.sh"; }

STOP_AFTER=8
DRY_RUN=0
EXTRA_FLAGS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --stop-after)
            [ $# -ge 2 ] || { echo "--stop-after needs a value (1-8)" >&2; usage >&2; exit 2; }
            case "$2" in
                ''|*[!0-9]) echo "--stop-after must be an integer 1-8 (got: $2)" >&2; exit 2 ;;
            esac
            [ "$2" -ge 1 ] && [ "$2" -le 8 ] || { echo "--stop-after must be 1-8 (got: $2)" >&2; exit 2; }
            STOP_AFTER="$2"; shift 2 ;;
        --stop-after=*)
            VAL="${1#*=}"
            case "$VAL" in
                ''|*[!0-9]) echo "--stop-after must be an integer 1-8 (got: $VAL)" >&2; exit 2 ;;
            esac
            [ "$VAL" -ge 1 ] && [ "$VAL" -le 8 ] || { echo "--stop-after must be 1-8 (got: $VAL)" >&2; exit 2; }
            STOP_AFTER="$VAL"; shift ;;
        --dry-run)         DRY_RUN=1; EXTRA_FLAGS="$EXTRA_FLAGS --dry-run"; shift ;;
        -a|--force-path-a) EXTRA_FLAGS="$EXTRA_FLAGS --force-path-a"; shift ;;
        -b|--force-path-b) EXTRA_FLAGS="$EXTRA_FLAGS --force-path-b"; shift ;;
        -h|--help)         usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# ------------------------------------------------------------------
# 0. Preconditions: run from the repo root, binary built.
# ------------------------------------------------------------------
cd "$SCRIPT_DIR/.." || fail "cannot reach the repo root from $SCRIPT_DIR"
[ -x ./tr4mpass ] || fail "./tr4mpass is not built -- run make first"

# ------------------------------------------------------------------
# 1. Env validation, BEFORE running anything.
#    lib.sh's require_* helpers exit 1 on the first missing item
#    (that is the lib.sh contract), so the first failure aborts here.
# ------------------------------------------------------------------
# shellcheck source=scripts/lib.sh
source "$SCRIPT_DIR/lib.sh"

require_env_var TR4MPASS_IBSS_PATH
require_file    iBSS "$TR4MPASS_IBSS_PATH"
require_env_var TR4MPASS_IBEC_PATH
require_file    iBEC "$TR4MPASS_IBEC_PATH"
require_env_var TR4MPASS_DEVICETREE_PATH
require_file    devicetree "$TR4MPASS_DEVICETREE_PATH"
require_env_var TR4MPASS_RAMDISK_PATH
require_file    ramdisk "$TR4MPASS_RAMDISK_PATH"

# Activation steps ([5/8]+) push a patched mobileactivationd to the
# device; below stage B it is simply passed through if set.
if [ "$STOP_AFTER" -ge 5 ]; then
    require_env_var TR4MPASS_PATCHED_MAD
    require_file    patched-mobileactivationd "$TR4MPASS_PATCHED_MAD"
elif [ -n "${TR4MPASS_PATCHED_MAD:-}" ]; then
    info "TR4MPASS_PATCHED_MAD is set but stop-after < 5; passing it through."
fi

# Trustcache is only needed on iOS 14+; optional, but validated if set.
if [ -n "${TR4MPASS_TRUSTCACHE_PATH:-}" ]; then
    require_file trustcache "$TR4MPASS_TRUSTCACHE_PATH"
fi

export TR4MPASS_STOP_AFTER="$STOP_AFTER"

info "stop-after: $STOP_AFTER"
info "state dir:  $(state_dir)"
LOGFILE="$(log_dir)/run-$(date +%Y%m%d-%H%M%S).log"
info "log file:   $LOGFILE"

# ------------------------------------------------------------------
# 2. Run the pipeline, tee the output, keep the real exit status.
# ------------------------------------------------------------------
if [ "$DRY_RUN" -eq 1 ]; then
    info "dry-run: ./tr4mpass -v$EXTRA_FLAGS (TR4MPASS_STOP_AFTER=$STOP_AFTER)"
fi

# shellcheck disable=SC2086
./tr4mpass -v$EXTRA_FLAGS 2>&1 | tee "$LOGFILE"
STATUS="${PIPESTATUS[0]}"

if [ "$STATUS" -ne 0 ]; then
    echo "FAIL: pipeline exited with status $STATUS (log: $LOGFILE)" >&2
else
    info "pipeline finished OK (log: $LOGFILE)"
fi

# ------------------------------------------------------------------
# 3. Post-run device state: PWND / activation visible in the serial.
# ------------------------------------------------------------------
info "current USB serial line(s):"
ioreg -p IOUSB -l -w0 2>/dev/null \
    | grep -i 'USB Serial Number' \
    || info "(no USB serial found -- device may have rebooted or left DFU)"

exit "$STATUS"

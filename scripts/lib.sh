#!/usr/bin/env bash
#
# lib.sh -- single source of truth for tr4mpass script state.
#
# Contract (sourced by every scripts/*.sh that keeps state):
#   TR4MPASS_STATE_DIR  env-overridable state root; default
#                       "${HOME}/.cache/tr4mpass". Never /tmp: everything
#                       a script writes lives in the repo layout or under
#                       this dir (ios_*.txt detection cache, images/,
#                       logs/).
#   state_dir           echo the state root, mkdir -p it first.
#   log_dir             echo "$(state_dir)/logs", mkdir -p it first.
#   require_env_var N   exit 1 with a stderr message naming N if the env
#                       var is unset or empty.
#   require_file L P    exit 1 with a stderr message naming L if P is not
#                       a readable regular file.
#
# No other helpers here on purpose (KISS). Note: the require_* helpers
# call exit 1, so they abort the sourcing script immediately -- that is
# the contract, not a bug.
set -u

TR4MPASS_STATE_DIR="${TR4MPASS_STATE_DIR:-${HOME}/.cache/tr4mpass}"

state_dir() {
    if ! mkdir -p "$TR4MPASS_STATE_DIR" 2>/dev/null; then
        echo "FAIL: cannot create state dir: $TR4MPASS_STATE_DIR" >&2
        exit 1
    fi
    printf '%s\n' "$TR4MPASS_STATE_DIR"
}

log_dir() {
    local logs="$TR4MPASS_STATE_DIR/logs"
    if ! mkdir -p "$logs" 2>/dev/null; then
        echo "FAIL: cannot create log dir: $logs" >&2
        exit 1
    fi
    printf '%s\n' "$logs"
}

require_env_var() {
    local name="$1" val="${!1:-}"
    if [ -z "$val" ]; then
        echo "FAIL: required environment variable not set or empty: $name" >&2
        exit 1
    fi
}

require_file() {
    local label="$1" path="$2"
    if [ -z "$path" ] || [ ! -f "$path" ] || [ ! -r "$path" ]; then
        echo "FAIL: $label: not a readable regular file: ${path:-<empty>}" >&2
        exit 1
    fi
}

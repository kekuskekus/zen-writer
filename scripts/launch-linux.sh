#!/usr/bin/env bash
set -euo pipefail
umask 077

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/zen-writer"
state_dir="${XDG_STATE_HOME:-${HOME}/.local/state}/zen-writer"
if [[ "${state_dir}" != /* ]]; then
    printf 'XDG_STATE_HOME must be an absolute path.\n' >&2
    exit 1
fi
mkdir -p "${state_dir}"
log_file="${state_dir}/launch.log"
printf 'Zen Writer log: %s\n' "${log_file}" >&2
exec 3>&2
exec >> "${log_file}" 2>&1

autostart=false
if [[ "${1:-}" == --autostart ]]; then
    autostart=true
    shift
fi
printf '\n[%s] Launch: autostart=%s desktop=%s session=%s\n' \
    "$(date --iso-8601=seconds)" "${autostart}" \
    "${XDG_CURRENT_DESKTOP:-unset}" "${XDG_SESSION_TYPE:-unset}"

fail() {
    printf 'ERROR: %s\n' "$1"
    printf 'Zen Writer: %s See %s\n' "$1" "${log_file}" >&3
    exit "${2:-1}"
}

if [[ "${autostart}" == true ]]; then
    # Hold this lock for the lifetime of the application, not just the wrapper.
    exec 9> "${state_dir}/autostart.lock"
    command -v flock >/dev/null || fail 'flock is missing; reinstall util-linux.' 127
    if ! flock --nonblock 9; then
        printf 'Another autostart instance is already running; skipping this duplicate.\n'
        exit 0
    fi
fi

[[ -x "${binary}" ]] || fail "Executable not found: ${binary}. Re-run the installer." 127

# Respect an explicit user/diagnostic override (including offscreen in tests).
if [[ -z "${QT_QPA_PLATFORM:-}" ]]; then
    if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
        export QT_QPA_PLATFORM='wayland;xcb'
    elif [[ -n "${DISPLAY:-}" ]]; then
        export QT_QPA_PLATFORM=xcb
    else
        fail 'No graphical display. Start from the desktop session, not a plain SSH/TTY session.'
    fi
fi
printf 'Qt platform: %s; DISPLAY=%s; WAYLAND_DISPLAY=%s\n' \
    "${QT_QPA_PLATFORM}" "${DISPLAY:-unset}" "${WAYLAND_DISPLAY:-unset}"

if "${binary}" "$@"; then
    printf '[%s] Application exited normally.\n' "$(date --iso-8601=seconds)"
else
    status=$?
    fail "Application exited with code ${status}." "${status}"
fi

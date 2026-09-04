#!/usr/bin/env bash
set -euo pipefail

if (( EUID == 0 )); then
    printf 'Run this installer as your desktop user, without sudo. It asks for sudo only when needed.\n' >&2
    exit 1
fi

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_dir}/build"

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    python3 \
    util-linux \
    qt6-base-dev \
    qt6-wayland \
    libhunspell-dev \
    hunspell-en-us \
    hunspell-ru

cmake -S "${repo_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DZEN_WRITER_BUILD_TESTS=ON
cmake --build "${build_dir}" --parallel "${ZEN_WRITER_BUILD_JOBS:-2}"
ctest --test-dir "${build_dir}" --output-on-failure
sudo cmake --install "${build_dir}"

QT_QPA_PLATFORM=offscreen /usr/local/bin/zen-writer --version
test -x /usr/local/bin/zen-writer-launch
python3 "${repo_dir}/scripts/configure-autostart.py"

if command -v raspi-config >/dev/null 2>&1; then
    sudo raspi-config nonint do_boot_behaviour B4
fi

printf '\nZen Writer installed. Reboot to test appliance mode.\n'
printf 'Ctrl+Q exits to the desktop; Ctrl+Shift+Q safely powers off.\n'
printf 'Test now (from your desktop terminal): /usr/local/bin/zen-writer-launch --fullscreen\n'
printf 'Launch log: %s/zen-writer/launch.log\n' "${XDG_STATE_HOME:-${HOME}/.local/state}"

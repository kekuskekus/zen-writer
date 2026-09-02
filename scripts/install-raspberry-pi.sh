#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_dir}/build"

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    qt6-base-dev \
    libhunspell-dev \
    hunspell-en-us \
    hunspell-ru

cmake -S "${repo_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DZEN_WRITER_BUILD_TESTS=ON
cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure
sudo cmake --install "${build_dir}"

autostart_line="/usr/local/bin/zen-writer --fullscreen &"
labwc_dir="${HOME}/.config/labwc"
labwc_file="${labwc_dir}/autostart"
xdg_dir="${HOME}/.config/autostart"

if command -v labwc >/dev/null 2>&1; then
    mkdir -p "${labwc_dir}"
    touch "${labwc_file}"
    if ! grep -Fqx "${autostart_line}" "${labwc_file}"; then
        printf '\n%s\n' "${autostart_line}" >> "${labwc_file}"
    fi
else
    mkdir -p "${xdg_dir}"
    install -m 0644 "${repo_dir}/resources/zen-writer.desktop" \
        "${xdg_dir}/zen-writer.desktop"
fi

if command -v raspi-config >/dev/null 2>&1; then
    sudo raspi-config nonint do_boot_behaviour B4
fi

printf '\nZen Writer installed. Reboot to test appliance mode.\n'
printf 'Ctrl+Q exits to the desktop; Ctrl+Shift+Q safely powers off.\n'

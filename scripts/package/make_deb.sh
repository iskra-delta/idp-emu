#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 STAGED_TREE VERSION OUTPUT_DIRECTORY" >&2
    exit 2
fi

stage=$(cd "$1" && pwd)
version=$2
output=$3
package_root=$(mktemp -d)
trap 'rm -rf "$package_root"' EXIT

install_root="$package_root/opt/iskra-delta-partner"
icon_theme_root="$package_root/usr/share/icons/hicolor"
mkdir -p "$install_root" "$package_root/DEBIAN" \
    "$package_root/usr/bin" "$package_root/usr/share/applications"
cp -a "$stage/." "$install_root/"
ln -s /opt/iskra-delta-partner/bin/idp-emu "$package_root/usr/bin/idp-emu"
ln -s /opt/iskra-delta-partner/bin/idp-mcp "$package_root/usr/bin/idp-mcp"

sed "s/@VERSION@/$version/g" packaging/linux/control.in > "$package_root/DEBIAN/control"
cp packaging/linux/idp-emu.desktop "$package_root/usr/share/applications/"
for size in 16 24 32 48 64 128; do
    icon_root="$icon_theme_root/${size}x${size}/apps"
    mkdir -p "$icon_root"
    cp "$stage/assets/icons/partner-${size}.png" \
        "$icon_root/iskra-delta-partner.png"
done
icon_root="$icon_theme_root/256x256/apps"
mkdir -p "$icon_root"
cp "$stage/assets/icons/partner.png" \
    "$icon_root/iskra-delta-partner.png"

# Fail before creating a package if any launcher/runtime relationship was lost.
test -x "$install_root/bin/idp-emu"
test -x "$install_root/bin/idp-mcp"
for size in 16 24 32 48 64 128 256; do
    test -s "$icon_theme_root/${size}x${size}/apps/iskra-delta-partner.png"
done
grep -Fqx 'Icon=iskra-delta-partner' \
    "$package_root/usr/share/applications/idp-emu.desktop"
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate \
        "$package_root/usr/share/applications/idp-emu.desktop"
fi
cmp -s "$stage/partner_cmos.bin" "$install_root/partner_cmos.bin"

mkdir -p "$output"
deb="$output/iskra-delta-partner_${version}_amd64.deb"
dpkg-deb --build --root-owner-group "$package_root" "$deb"
dpkg-deb --info "$deb" >/dev/null

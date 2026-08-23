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
install -m 755 packaging/linux/partnerp "$install_root/bin/partnerp"
install -m 755 packaging/linux/partnerg "$install_root/bin/partnerg"
ln -s /opt/iskra-delta-partner/bin/partnerp "$package_root/usr/bin/partnerp"
ln -s /opt/iskra-delta-partner/bin/partnerg "$package_root/usr/bin/partnerg"

sed "s/@VERSION@/$version/g" packaging/linux/control.in > "$package_root/DEBIAN/control"
cp packaging/linux/partnerp.desktop "$package_root/usr/share/applications/"
cp packaging/linux/partnerg.desktop "$package_root/usr/share/applications/"
cp packaging/linux/idp-mcp.desktop "$package_root/usr/share/applications/"
for size in 16 24 32 48 64 128; do
    icon_root="$icon_theme_root/${size}x${size}/apps"
    mkdir -p "$icon_root"
    cp "$stage/assets/icons/partner-${size}.png" \
        "$icon_root/iskra-delta-partner.png"
    cp "$stage/assets/icons/mcp-${size}.png" \
        "$icon_root/iskra-delta-partner-mcp.png"
done
icon_root="$icon_theme_root/256x256/apps"
mkdir -p "$icon_root"
cp "$stage/assets/icons/partner.png" \
    "$icon_root/iskra-delta-partner.png"
cp "$stage/assets/icons/mcp.png" \
    "$icon_root/iskra-delta-partner-mcp.png"

# Fail before creating a package if any launcher/runtime relationship was lost.
test -x "$install_root/bin/idp-emu"
test -x "$install_root/bin/idp-mcp"
test -x "$install_root/bin/partnerp"
test -x "$install_root/bin/partnerg"
"$install_root/bin/partnerp" --help >/dev/null 2>&1
"$install_root/bin/partnerg" --help >/dev/null 2>&1
grep -Fq -- '--model crt --system-crt-hdd "$@"' \
    "$install_root/bin/partnerp"
grep -Fq -- '--model gdp --system-hdd "$@"' \
    "$install_root/bin/partnerg"
if grep -Fq -- 'socat' "$package_root/DEBIAN/control"; then
    echo "obsolete socat dependency remains in Debian control file" >&2
    exit 1
fi
for size in 16 24 32 48 64 128 256; do
    test -s "$icon_theme_root/${size}x${size}/apps/iskra-delta-partner.png"
    test -s "$icon_theme_root/${size}x${size}/apps/iskra-delta-partner-mcp.png"
done
grep -Fqx 'Exec=/usr/bin/partnerp' \
    "$package_root/usr/share/applications/partnerp.desktop"
grep -Fqx 'Exec=/usr/bin/partnerg' \
    "$package_root/usr/share/applications/partnerg.desktop"
grep -Fqx 'Icon=iskra-delta-partner' \
    "$package_root/usr/share/applications/partnerp.desktop"
grep -Fqx 'Icon=iskra-delta-partner' \
    "$package_root/usr/share/applications/partnerg.desktop"
grep -Fqx 'Icon=iskra-delta-partner-mcp' \
    "$package_root/usr/share/applications/idp-mcp.desktop"
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate \
        "$package_root/usr/share/applications/partnerp.desktop"
    desktop-file-validate \
        "$package_root/usr/share/applications/partnerg.desktop"
    desktop-file-validate \
        "$package_root/usr/share/applications/idp-mcp.desktop"
fi
cmp -s "$stage/partner_cmos.bin" "$install_root/partner_cmos.bin"
test ! -e "$package_root/usr/bin/partner"
test ! -e "$package_root/usr/share/applications/partner.desktop"
mapfile -t packaged_disks < <(find "$install_root/disks" -mindepth 1 -maxdepth 1 \
    -printf '%f\n' | sort)
test "${packaged_disks[*]}" = \
    "hdd-partner-g-system.img hdd-partner-p-system.img"

mkdir -p "$output"
deb="$output/iskra-delta-partner_${version}_amd64.deb"
dpkg-deb --build --root-owner-group "$package_root" "$deb"
dpkg-deb --info "$deb" >/dev/null

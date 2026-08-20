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
mkdir -p "$install_root" "$package_root/DEBIAN" \
    "$package_root/usr/bin" "$package_root/usr/share/applications"
cp -a "$stage/." "$install_root/"
ln -s /opt/iskra-delta-partner/bin/idp-emu "$package_root/usr/bin/idp-emu"
ln -s /opt/iskra-delta-partner/bin/idp-mcp "$package_root/usr/bin/idp-mcp"

sed "s/@VERSION@/$version/g" packaging/linux/control.in > "$package_root/DEBIAN/control"
cp packaging/linux/idp-emu.desktop "$package_root/usr/share/applications/"

mkdir -p "$output"
dpkg-deb --build --root-owner-group "$package_root" \
    "$output/iskra-delta-partner_${version}_amd64.deb"

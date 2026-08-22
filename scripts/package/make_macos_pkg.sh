#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 STAGED_TREE VERSION OUTPUT_DIRECTORY ARCHITECTURE" >&2
    exit 2
fi

stage=$(cd "$1" && pwd)
version=$2
output=$3
architecture=$4
payload=$(mktemp -d)
trap 'rm -rf "$payload"' EXIT

contents="$payload/Applications/Iskra Delta Partner.app/Contents"
mkdir -p "$contents/MacOS" "$contents/shared" "$contents/Resources" \
    "$payload/usr/local/bin"
cp -a "$stage/bin/." "$contents/MacOS/"
cp -a "$stage/shared/." "$contents/shared/"
for directory in assets roms disks docs; do
    cp -a "$stage/$directory" "$contents/Resources/$directory"
done
cp "$stage/partner_cmos.bin" "$contents/Resources/partner_cmos.bin"
iconutil -c icns -o "$contents/Resources/partner.icns" \
    packaging/icons/partner.iconset

sed -e "s/@VERSION@/$version/g" packaging/macos/Info.plist.in > "$contents/Info.plist"
plutil -lint "$contents/Info.plist"
test -x "$contents/MacOS/idp-emu"
test -x "$contents/MacOS/idp-mcp"
test -s "$contents/Resources/partner.icns"
cmp -s "$stage/partner_cmos.bin" "$contents/Resources/partner_cmos.bin"
codesign --force --deep --sign - --timestamp=none \
    "$payload/Applications/Iskra Delta Partner.app"
codesign --verify --deep --strict \
    "$payload/Applications/Iskra Delta Partner.app"
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/idp-emu" \
    "$payload/usr/local/bin/idp-emu"
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/idp-mcp" \
    "$payload/usr/local/bin/idp-mcp"

mkdir -p "$output"
pkg="$output/Iskra-Delta-Partner-${version}-macOS-${architecture}.pkg"
pkgbuild --root "$payload" \
    --identifier org.iskradelta.partner-emulator \
    --version "$version" \
    --install-location / \
    "$pkg"
pkgutil --payload-files "$pkg" >/dev/null

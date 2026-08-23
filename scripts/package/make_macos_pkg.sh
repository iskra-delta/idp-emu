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

app="$payload/Applications/Iskra Delta Partner P.app"
partnerg_app="$payload/Applications/Iskra Delta Partner G.app"
mcp_app="$payload/Applications/Iskra Delta Partner MCP.app"
contents="$app/Contents"
partnerg_contents="$partnerg_app/Contents"
mcp_contents="$mcp_app/Contents"
mkdir -p "$contents/MacOS" "$contents/shared" "$contents/Resources" \
    "$partnerg_contents/MacOS" "$partnerg_contents/Resources" \
    "$mcp_contents/MacOS" "$mcp_contents/Resources" \
    "$payload/usr/local/bin"
cp -a "$stage/bin/." "$contents/MacOS/"
rm -f "$contents/MacOS/partnerp" "$contents/MacOS/partnerg"
cp -a "$stage/shared/." "$contents/shared/"
for directory in assets roms disks docs; do
    cp -a "$stage/$directory" "$contents/Resources/$directory"
done
cp "$stage/partner_cmos.bin" "$contents/Resources/partner_cmos.bin"
iconutil -c icns -o "$contents/Resources/partner.icns" \
    packaging/icons/partner.iconset
iconutil -c icns -o "$partnerg_contents/Resources/partner.icns" \
    packaging/icons/partner.iconset
iconutil -c icns -o "$mcp_contents/Resources/mcp.icns" \
    packaging/icons/mcp.iconset

compile_launcher() {
    local profile=$1
    local destination=$2
    xcrun --sdk macosx clang -std=c17 -Os -Wall -Wextra -Werror \
        -mmacosx-version-min=12.0 "-DIDP_LAUNCH_PROFILE=$profile" \
        packaging/macos/bundle-launcher.c -o "$destination"
}
compile_launcher 1 "$contents/MacOS/partnerp"
compile_launcher 2 "$partnerg_contents/MacOS/partnerg"
compile_launcher 0 "$mcp_contents/MacOS/idp-mcp"

sed -e "s/@VERSION@/$version/g" packaging/macos/Info.plist.in > "$contents/Info.plist"
sed -e "s/@VERSION@/$version/g" packaging/macos/PartnerG-Info.plist.in > "$partnerg_contents/Info.plist"
sed -e "s/@VERSION@/$version/g" packaging/macos/MCP-Info.plist.in > "$mcp_contents/Info.plist"
plutil -lint "$contents/Info.plist"
plutil -lint "$partnerg_contents/Info.plist"
plutil -lint "$mcp_contents/Info.plist"
test -x "$contents/MacOS/idp-emu"
test -x "$contents/MacOS/idp-mcp"
test -x "$contents/MacOS/partnerp"
test -x "$partnerg_contents/MacOS/partnerg"
test -x "$mcp_contents/MacOS/idp-mcp"
test ! -e "$contents/MacOS/partner"
test ! -e "$contents/MacOS/partnerg"
test -s "$contents/Resources/partner.icns"
test -s "$partnerg_contents/Resources/partner.icns"
test -s "$mcp_contents/Resources/mcp.icns"
test -s "$contents/Resources/assets/icons/partner.ico"
test -s "$contents/Resources/assets/icons/mcp.ico"
cmp -s "$stage/partner_cmos.bin" "$contents/Resources/partner_cmos.bin"
test -s "$contents/Resources/disks/hdd-partner-p-system.img"
test -s "$contents/Resources/disks/hdd-partner-g-system.img"
"$contents/MacOS/partnerp" --help >/dev/null 2>&1
"$partnerg_contents/MacOS/partnerg" --help >/dev/null 2>&1
test "$("$mcp_contents/MacOS/idp-mcp" --version)" = "idp-mcp $version"
"$mcp_contents/MacOS/idp-mcp" --list-tools >/dev/null
codesign --force --deep --sign - --timestamp=none \
    "$app"
codesign --force --deep --sign - --timestamp=none \
    "$partnerg_app"
codesign --force --deep --sign - --timestamp=none \
    "$mcp_app"
codesign --verify --deep --strict \
    "$app"
codesign --verify --deep --strict \
    "$partnerg_app"
codesign --verify --deep --strict \
    "$mcp_app"
ln -s "/Applications/Iskra Delta Partner P.app/Contents/MacOS/idp-emu" \
    "$payload/usr/local/bin/idp-emu"
ln -s "/Applications/Iskra Delta Partner P.app/Contents/MacOS/idp-mcp" \
    "$payload/usr/local/bin/idp-mcp"
install -m 755 "$stage/bin/partnerp" "$payload/usr/local/bin/partnerp"
install -m 755 "$stage/bin/partnerg" "$payload/usr/local/bin/partnerg"

mkdir -p "$output"
pkg="$output/Iskra-Delta-Partner-${version}-macOS-${architecture}.pkg"
pkgbuild --root "$payload" \
    --scripts packaging/macos/scripts \
    --identifier org.iskradelta.partner-emulator \
    --version "$version" \
    --install-location / \
    "$pkg"
pkgutil --payload-files "$pkg" >/dev/null

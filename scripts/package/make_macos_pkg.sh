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

app="$payload/Applications/Iskra Delta Partner.app"
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
compile_launcher 1 "$contents/MacOS/partner"
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
test -x "$contents/MacOS/partner"
test -x "$partnerg_contents/MacOS/partnerg"
test -x "$mcp_contents/MacOS/idp-mcp"
test -s "$contents/Resources/partner.icns"
test -s "$partnerg_contents/Resources/partner.icns"
test -s "$mcp_contents/Resources/mcp.icns"
test -s "$contents/Resources/assets/icons/partner.ico"
test -s "$contents/Resources/assets/icons/mcp.ico"
cmp -s "$stage/partner_cmos.bin" "$contents/Resources/partner_cmos.bin"
"$contents/MacOS/partner" --help >/dev/null 2>&1
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
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/idp-emu" \
    "$payload/usr/local/bin/idp-emu"
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/idp-mcp" \
    "$payload/usr/local/bin/idp-mcp"
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/partner" \
    "$payload/usr/local/bin/partner"
ln -s "/Applications/Iskra Delta Partner G.app/Contents/MacOS/partnerg" \
    "$payload/usr/local/bin/partnerg"

mkdir -p "$output"
pkg="$output/Iskra-Delta-Partner-${version}-macOS-${architecture}.pkg"
pkgbuild --root "$payload" \
    --identifier org.iskradelta.partner-emulator \
    --version "$version" \
    --install-location / \
    "$pkg"
pkgutil --payload-files "$pkg" >/dev/null

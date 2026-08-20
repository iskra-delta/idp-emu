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

sed -e "s/@VERSION@/$version/g" packaging/macos/Info.plist.in > "$contents/Info.plist"
codesign --force --deep --sign - --timestamp=none \
    "$payload/Applications/Iskra Delta Partner.app"
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/idp-emu" \
    "$payload/usr/local/bin/idp-emu"
ln -s "/Applications/Iskra Delta Partner.app/Contents/MacOS/idp-mcp" \
    "$payload/usr/local/bin/idp-mcp"

mkdir -p "$output"
pkgbuild --root "$payload" \
    --identifier org.iskradelta.partner-emulator \
    --version "$version" \
    --install-location / \
    "$output/Iskra-Delta-Partner-${version}-macOS-${architecture}.pkg"

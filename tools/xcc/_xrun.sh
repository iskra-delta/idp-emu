#!/bin/sh
# Common runner for the x compiler suite, executed inside the pinned docker
# image wischner/xcc-z80. We mount the developer's data root at the SAME
# absolute path inside the container so that both absolute paths (the partos
# Makefile passes $(abspath ...) for outputs) and the current working
# directory resolve identically on host and in the container.
#
# Only this image is used for the toolchain, per project decision. Override the
# tag with XCC_IMAGE if needed.
set -e
XCC_IMAGE="${XCC_IMAGE:-wischner/xcc-z80:1.9.3}"
MOUNT_ROOT="${XCC_MOUNT_ROOT:-/home/tstih/data}"
tool="$1"; shift
exec docker run --rm \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "${MOUNT_ROOT}:${MOUNT_ROOT}" \
    -w "$PWD" \
    "$XCC_IMAGE" "$tool" "$@"

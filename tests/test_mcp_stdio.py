#!/usr/bin/env python3
import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib


def request(message_id, method, params=None):
    value = {"jsonrpc": "2.0", "id": message_id, "method": method}
    if params is not None:
        value["params"] = params
    return json.dumps(value, separators=(",", ":"))


def call(message_id, name, arguments=None):
    return request(message_id, "tools/call", {
        "name": name,
        "arguments": arguments or {},
    })


def structured(replies, message_id):
    result = replies[message_id]["result"]
    if result.get("isError"):
        raise AssertionError(
            f"tool call {message_id} failed: {result['content'][0]['text']}")
    return result["structuredContent"]


def png_dimensions(data):
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("invalid PNG data")
    return struct.unpack(">II", data[16:24])


def decode_png_rgb(data):
    width, height = png_dimensions(data)
    compressed = bytearray()
    position = 8
    while position + 12 <= len(data):
        size = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + size]
        if kind == b"IHDR":
            bit_depth, color_type, compression, filtering, interlace = payload[8:]
            if (bit_depth, color_type, compression, filtering, interlace) != (
                    8, 2, 0, 0, 0):
                raise AssertionError("PNG is not non-interlaced 8-bit RGB")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        position += size + 12

    encoded = zlib.decompress(compressed)
    stride = width * 3
    previous = bytearray(stride)
    pixels = bytearray()
    position = 0

    def paeth(left, above, upper_left):
        prediction = left + above - upper_left
        distances = (abs(prediction - left), abs(prediction - above),
                     abs(prediction - upper_left))
        if distances[0] <= distances[1] and distances[0] <= distances[2]:
            return left
        return above if distances[1] <= distances[2] else upper_left

    for _ in range(height):
        filter_type = encoded[position]
        position += 1
        raw = encoded[position:position + stride]
        position += stride
        row = bytearray(stride)
        for index, value in enumerate(raw):
            left = row[index - 3] if index >= 3 else 0
            above = previous[index]
            upper_left = previous[index - 3] if index >= 3 else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = paeth(left, above, upper_left)
            else:
                raise AssertionError(f"unsupported PNG filter {filter_type}")
            row[index] = (value + predictor) & 0xFF
        pixels.extend(row)
        previous = row
    return width, height, bytes(pixels)


def main():
    executable = sys.argv[1]
    dump_root = os.path.join(os.path.dirname(__file__), "dump")
    os.makedirs(dump_root, exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="idp-mcp-test-", dir=dump_root) as temporary:
        screenshot_path = os.path.join(temporary, "screen.png")
        video_path = os.path.join(temporary, "screen.y4m")
        messages = [
            request(1, "initialize", {
                "protocolVersion": "2025-06-18",
                "clientInfo": {"name": "ctest", "version": "1"},
                "capabilities": {},
            }),
            json.dumps({"jsonrpc": "2.0",
                        "method": "notifications/initialized"}),
            request(2, "tools/list"),
            call(3, "status"),
            call(4, "reset", {"clear_memory": True}),
            # NOP; LD A,(9000h); HALT. Numeric strings and hex byte strings
            # deliberately exercise compatibility with zx-spectrum-mcp.
            call(5, "write_memory", {
                "address": "0x8000", "data": "00 3a 00 90 76"}),
            call(6, "write_memory", {"address": "$9000", "data": [0x42]}),
            call(7, "read_memory", {"address": "#8000", "length": 5}),
            call(8, "registers", {
                "pc": "$8000", "af_alt": "#1234", "im": 2}),
            call(9, "measure_cycles", {"instructions": 1}),
            call(10, "breakpoint", {
                "action": "add", "kind": "memory_read",
                "address": "0x9000", "value": 0x42}),
            call(11, "run", {"instructions": 10, "max_ticks": 1000}),
            call(12, "breakpoint", {"action": "list"}),
            call(13, "breakpoint", {"action": "clear"}),
            call(14, "registers", {"pc": 0x8000}),
            call(15, "run_until", {
                "address": 0x8001, "max_tstates": 100}),
            call(16, "step"),
            call(17, "load", {
                "data": "00", "format": "binary",
                "address": 0xA000, "start": 0xA000}),
            call(18, "measure_cycles", {"address": 0xA000}),
            call(19, "read_port", {"port": "0x77"}),
            call(20, "set_port", {"port": "0x77", "value": 0xA5}),
            call(21, "keyboard", {"text": "x"}),
            call(22, "press_keys", {
                "keys": ["ENTER"], "hold_frames": 1, "gap_frames": 0}),
            call(23, "screen"),
            call(24, "screen_text", {
                "mode": "ascii", "columns": 32, "rows": 12}),
            call(25, "screenshot", {"path": screenshot_path}),
            call(26, "video_start", {"path": video_path}),
            call(27, "run", {"frames": 1}),
            call(28, "video_stop"),
            request(29, "ping"),
        ]
        completed = subprocess.run(
            [executable, "--model", "crt"],
            input="\n".join(messages) + "\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"server exited {completed.returncode}: {completed.stderr}")
        replies_list = [json.loads(line) for line in completed.stdout.splitlines()
                        if line]
        expected_ids = list(range(1, 30))
        if [reply.get("id") for reply in replies_list] != expected_ids:
            raise AssertionError(
                f"notification replied or stdout was polluted: {completed.stdout!r}")
        replies = {reply["id"]: reply for reply in replies_list}

        server_info = replies[1]["result"]["serverInfo"]
        if server_info.get("name") != "idp-mcp" or not server_info.get("version"):
            raise AssertionError(f"bad initialize result: {server_info}")

        names = {entry["name"] for entry in replies[2]["result"]["tools"]}
        spectrum_compatible = {
            "load", "reset", "run", "run_until", "step", "status",
            "read_memory", "write_memory", "registers", "breakpoint",
            "press_keys", "read_port", "set_port", "screen", "screen_text",
            "screenshot", "video_start", "video_stop",
        }
        partner_specific = {
            "measure_cycles", "read_io", "write_io", "keyboard",
            "mount_media",
        }
        missing = (spectrum_compatible | partner_specific) - names
        if missing:
            raise AssertionError(f"missing MCP tools: {missing}")
        if "tape" in names:
            raise AssertionError("cassette tool must not be exposed on Partner hardware")

        status = structured(replies, 3)
        if status["clock_hz"] != 4_000_000 or status["cycles"] != 0:
            raise AssertionError(f"bad timing metadata: {status}")
        if structured(replies, 7)["data"] != [0x00, 0x3A, 0x00, 0x90, 0x76]:
            raise AssertionError("hex write/read or numeric address strings failed")
        registers = structured(replies, 8)
        if (registers["pc"], registers["af_alt"], registers["im"]) != (
                0x8000, 0x1234, 2):
            raise AssertionError(f"complete register update failed: {registers}")

        nop = structured(replies, 9)["measurement"]
        if (nop["cycles"], nop["tstates"], nop["instructions"], nop["pc"]) != (
                4, 4, 1, 0x8001):
            raise AssertionError(f"NOP must measure exactly four cycles: {nop}")
        watched_read = structured(replies, 11)["run"]
        if (watched_read["reason"], watched_read["cycles"],
                watched_read.get("breakpoint_id"), watched_read["pc"]) != (
                "breakpoint", 13, 1, 0x8004):
            raise AssertionError(
                f"LD A,(nn) signal breakpoint/timing failed: {watched_read}")
        listed = structured(replies, 12)["breakpoints"]
        if len(listed) != 1 or listed[0]["hits"] != 1:
            raise AssertionError(f"breakpoint hit count failed: {listed}")

        until = structured(replies, 15)["run"]
        if (until["reason"], until["cycles"], until["pc"]) != (
                "address_reached", 4, 0x8001):
            raise AssertionError(f"run_until failed: {until}")
        step = structured(replies, 16)["run"]
        if (step["cycles"], step["instructions"], step["pc"]) != (13, 1, 0x8004):
            raise AssertionError(f"instruction step timing failed: {step}")
        loaded_nop = structured(replies, 18)["measurement"]
        if loaded_nop["cycles"] != 4 or loaded_nop["pc"] != 0xA001:
            raise AssertionError(f"inline load/start or measurement failed: {loaded_nop}")

        if structured(replies, 19)["cycles"] != 0:
            raise AssertionError("read_port unexpectedly advanced emulated time")
        if structured(replies, 20)["cycles"] != 0:
            raise AssertionError("set_port unexpectedly advanced emulated time")
        if structured(replies, 21)["accepted"] != 1:
            raise AssertionError("raw Partner keyboard byte was not accepted")
        pressed = structured(replies, 22)
        if pressed["accepted"] != 1 or pressed["cycles"] != 66_667:
            raise AssertionError(f"timed serial key input failed: {pressed}")

        screen_result = replies[23]["result"]
        screen_info = structured(replies, 23)
        image_parts = [part for part in screen_result["content"]
                       if part["type"] == "image"]
        if len(image_parts) != 1 or image_parts[0]["mimeType"] != "image/png":
            raise AssertionError("screen did not return one PNG image content block")
        png = base64.b64decode(image_parts[0]["data"], validate=True)
        if png[:8] != b"\x89PNG\r\n\x1a\n" or screen_info["bytes"] != len(png):
            raise AssertionError("screen PNG is invalid or metadata is wrong")
        ascii_screen = structured(replies, 24)
        if ascii_screen["columns"] != 32 or ascii_screen["rows"] != 12:
            raise AssertionError("ASCII screen geometry was ignored")

        with open(screenshot_path, "rb") as screenshot:
            if screenshot.read(8) != b"\x89PNG\r\n\x1a\n":
                raise AssertionError("screenshot did not write a PNG")
        video = structured(replies, 28)
        if video["frames"] != 1 or video["bytes"] <= 1_000_000:
            raise AssertionError(f"video did not capture one full frame: {video}")
        with open(video_path, "rb") as recording:
            header = recording.readline()
            frame_header = recording.readline()
        if not header.startswith(b"YUV4MPEG2 W880 H384 F60:1"):
            raise AssertionError(f"bad Y4M header: {header!r}")
        if frame_header != b"FRAME\n":
            raise AssertionError("Y4M recording has no frame")

        gdp_full_path = os.path.join(temporary, "gdp-full.png")
        gdp_active_path = os.path.join(temporary, "gdp-active.png")
        gdp_messages = [
            request(1, "initialize", {
                "protocolVersion": "2025-06-18",
                "clientInfo": {"name": "ctest", "version": "1"},
                "capabilities": {},
            }),
            json.dumps({"jsonrpc": "2.0",
                        "method": "notifications/initialized"}),
            call(2, "reset", {"clear_memory": True}),
            # Plot the top-left EF raster pixel to make crop-origin checks
            # observable even when the stopped machine's other planes are blank.
            call(3, "set_port", {"port": 0x21, "value": 0x03}),
            call(4, "set_port", {"port": 0x28, "value": 0x00}),
            call(5, "set_port", {"port": 0x29, "value": 0x00}),
            call(6, "set_port", {"port": 0x2A, "value": 0x01}),
            call(7, "set_port", {"port": 0x2B, "value": 0xFF}),
            call(8, "set_port", {"port": 0x20, "value": 0x80}),
            call(9, "screen", {"include_border": True}),
            call(10, "screen", {"include_border": False}),
            call(11, "screenshot", {
                "path": gdp_full_path, "include_border": True}),
            call(12, "screenshot", {
                "path": gdp_active_path, "include_border": False}),
            # Program GDP PIO port A as output, then select 256-line format.
            call(13, "set_port", {"port": 0x31, "value": 0x07}),
            call(14, "set_port", {"port": 0x31, "value": 0x0F}),
            call(15, "set_port", {"port": 0x30, "value": 0x00}),
            call(16, "screen", {"include_border": False}),
            call(17, "screen", {"include_border": True}),
        ]
        gdp_completed = subprocess.run(
            [executable, "--model", "gdp"],
            input="\n".join(gdp_messages) + "\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
        if gdp_completed.returncode != 0:
            raise AssertionError(
                f"GDP server exited {gdp_completed.returncode}: "
                f"{gdp_completed.stderr}")
        gdp_replies_list = [
            json.loads(line) for line in gdp_completed.stdout.splitlines() if line]
        if [reply.get("id") for reply in gdp_replies_list] != list(range(1, 18)):
            raise AssertionError(
                f"GDP notification replied or stdout was polluted: "
                f"{gdp_completed.stdout!r}")
        gdp_replies = {reply["id"]: reply for reply in gdp_replies_list}

        expected_geometry = {
            9: (1056, 624, True),
            10: (1024, 512, False),
            11: (1056, 624, True),
            12: (1024, 512, False),
            16: (1024, 256, False),
            17: (1056, 624, True),
        }
        for message_id, (width, height, include_border) in expected_geometry.items():
            info = structured(gdp_replies, message_id)
            actual = (info["width"], info["height"], info["include_border"])
            if actual != (width, height, include_border):
                raise AssertionError(
                    f"GDP capture {message_id} geometry is {actual}, expected "
                    f"{(width, height, include_border)}")

        screen_pngs = {}
        for message_id in (9, 10, 16, 17):
            image_parts = [
                part for part in gdp_replies[message_id]["result"]["content"]
                if part["type"] == "image"]
            if len(image_parts) != 1:
                raise AssertionError(
                    f"GDP screen {message_id} did not return one image")
            png = base64.b64decode(image_parts[0]["data"], validate=True)
            dimensions = png_dimensions(png)
            expected = expected_geometry[message_id][:2]
            if dimensions != expected:
                raise AssertionError(
                    f"GDP screen {message_id} PNG is {dimensions}, "
                    f"expected {expected}")
            screen_pngs[message_id] = decode_png_rgb(png)

        for full_id, active_id in ((9, 10), (17, 16)):
            full_width, _, full_pixels = screen_pngs[full_id]
            _, _, active_pixels = screen_pngs[active_id]
            full_offset = (56 * full_width + 16) * 3
            if active_pixels[:3] == b"\x00\x00\x00":
                raise AssertionError("GDP borderless image lost logical pixel (0, 0)")
            if active_pixels[:3] != full_pixels[full_offset:full_offset + 3]:
                raise AssertionError(
                    "GDP borderless image does not begin at logical pixel (0, 0)")

        for path, expected in ((gdp_full_path, (1056, 624)),
                               (gdp_active_path, (1024, 512))):
            with open(path, "rb") as screenshot:
                dimensions = png_dimensions(screenshot.read())
            if dimensions != expected:
                raise AssertionError(
                    f"GDP screenshot {path} is {dimensions}, expected {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
import base64
import json
import os
import subprocess
import sys
import tempfile


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


def main():
    executable = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="idp-mcp-test-") as temporary:
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
        if server_info != {"name": "idp-mcp", "version": "1.1.0"}:
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

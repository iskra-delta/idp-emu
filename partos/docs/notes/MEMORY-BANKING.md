# Note: Memory Banking and ROM Overlay Quirks

Category: Hardware / Banking / Boot
Date(s): 2026-06-11

Facts verified against the idp-emu emulator sources unless marked
otherwise. These quirks directly shape the PartOS boot sequence.

## Banking ports react to reads AND writes

Ports 0x80-0x97 switch state on any access — `IN` is as effective as
`OUT`. Consequences:

- Never use `IN` on 0x80-0x97 "just to look"; there is nothing to read
  (the bus returns 0xFF) and the side effect is a bank/ROM switch.
- A stray port scan (debug code, probing loops) can silently switch banks
  or kill the ROM overlay.

Port map:

| Port touch  | Effect                                   |
|-------------|------------------------------------------|
| 0x80 - 0x87 | Disable ROM overlay (one-way)            |
| 0x88 - 0x8F | Select RAM bank 1 (reset default)        |
| 0x90 - 0x97 | Select RAM bank 2                        |

## ROM disable is one-way

There is no port that re-enables the ROM overlay; only hardware reset does.
After boot establishes the runtime layout, the ROM content is gone until
reset. The page-0 copies and the BIOS in common RAM *are* the system from
that point on.

## Writes under the enabled ROM overlay are lost

While the overlay is on, writes to 0x0000-0x1FFF do not reach the RAM
underneath (verified in the emulator; assumed true for hardware). This is
why page 0 can only be installed **after** the overlay is disabled — which
in turn means the code doing it must already be running from common RAM.
Hence the boot order: copy BIOS high first, jump high, then ROM off, then
write page 0 into both banks.

If real hardware turns out to allow write-through under ROM, the ordering
could be relaxed — but the current order works on both interpretations,
so we keep it.

## ROM is mirrored 4x

The 2 KB ROM appears at 0x0000, 0x0800, 0x1000 and 0x1800 while the overlay
is enabled. Don't be confused by code "existing" at four addresses during
boot; at runtime only the page-0 copies (per bank) and the BIOS in common
RAM exist.

## Bank switching only affects 0x0000-0xBFFF

Code running in common RAM (0xC000+) or in replicated page 0 can switch
banks freely. Code running in 0x0100-0xBFFF must never switch banks (it
would pull the rug from under its own feet) — that is an OS-level rule.

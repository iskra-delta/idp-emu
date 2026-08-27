# Storage controller reference corpus

Date: 2026-08-26

## Scope

“All code” is bounded here to material that adds an observable S1410/SASI or
Intel 8272/uPD765 behavior:

- manufacturer manuals containing complete host listings or command examples;
- both original Partner boot ROM command streams and the CP/M BIOS paths in
  the shipped disk images;
- the motherboard and SASI KiCad reconstructions;
- two maintained open-source uPD765 implementations used only as differential
  cross-checks.

Unrelated SCSI controllers, PC-specific super-I/O wrappers, UI code, and
duplicate mirrors are outside the corpus. Manufacturer documentation and the
Partner schematics/ROMs take precedence over third-party implementations.

Downloads and OCR working files live in the gitignored lowercase directory
`tests/dump/storage-references`. They are deliberately not committed. The
table records stable retrieval links and hashes so the comparison is
reproducible.

## Manufacturer material

| File | Relevant content | SHA-256 |
| --- | --- | --- |
| [Xebec S1410 Owner's Manual, revision C](https://bitsavers.trailing-edge.com/pdf/xebec/Xebec_S1410/104524C_S1410Man_Aug83.pdf) | SASI phases, every DCB, status/sense formats, revision-E commands, complete Z80 format/write/read example | `790793db296587aef55d0702522a258bc2717fc5b57c9eccb01350c0b6f393a8` |
| [Intel iSBC 208 User's Manual](https://bitsavers.trailing-edge.com/pdf/intel/iSBC/143078-001_iSBC_208_Users_Manual_Oct81.pdf) | 8272 pin/phase/command programming and full host driver listing | `39b46b8e01064005b99a78aaecf6138c31b4604fa0eca165eb7a59bf74e0d634` |
| [Intel iSBX 218A Hardware Reference](https://bitsavers.trailing-edge.com/pdf/intel/iSBX/145911-001_iSBX_218_Flexible_Diskette_Controller_Hardware_Reference_Aug83.pdf) | 8272 programming cautions, EOT/TC behavior, scan silicon limitations | `2cb50a69996acc4c69f92112ecf2484bac6a2e3c78457c7e9299e91973aea796` |
| [Intel 8272 datasheet transcription](https://hxc2001.com/download/datasheet/floppy/thirdparty/FDC/Intel/I8272.doc) | Searchable command table, transfer/result rules, signal definitions | `b7199a36e6ef2631b1e00981a295c249f488361014a677f986b599ccf6d27c5b` |

The S1410 manual's Z80 listing was converted into minimal controller tests:
select, test ready, initialize, recalibrate, read/write, completion status and
message, error completion, and request sense. Intel's command and driver
sequences became the 8272 MSR, seek/sense, DMA/TC, multi-sector, read-track,
deleted read/write, format, read-ID, and scan regressions.

## Differential implementation references

These files are useful for finding state-machine omissions but are not
hardware authority:

| File | Retrieval link | SHA-256 |
| --- | --- | --- |
| floooh/chips `upd765.h` | [raw source](https://raw.githubusercontent.com/floooh/chips/master/chips/upd765.h) | `54ab556274c5f2fa48c1b0817f2bb9dab9dd9ac5d7df750c2abb13d32a160843` |
| MAME `upd765.cpp` | [raw source](https://raw.githubusercontent.com/mamedev/mame/master/src/devices/machine/upd765.cpp) | `c27a400a23f1f625ef39c58097a5c6328081b663c4e006c5e60008deb2be9b90` |
| MAME `upd765.h` | [raw source](https://raw.githubusercontent.com/mamedev/mame/master/src/devices/machine/upd765.h) | `66289363c14d4e52083e700d040610676628c587647859bcf7809425ce70789d` |

## Partner-specific evidence

- `roms/partner_crt.rom` and `roms/partner_gdp.rom` both send the documented
  S1410 `0Ch` DCB, eight drive-characteristic bytes, a six-byte recalibrate,
  and a 31-block read from LBA zero.
- Partner P's floppy path terminates one 256-byte read through DMA TC, with EOT
  set to the medium's last sector, and validates ST0 normal termination.
- Partner G sets EOT equal to the requested one-sector transfer and explicitly
  accepts ST1 `80h`; this is retained as a board-level EOB-to-TC observation.
- The motherboard establishes the FDC DRQ/DACK latch and
  `TC = BUSAK+ AND NOT(INT1-)`. The SASI adapter establishes the control latch,
  status-bit polarity, and expansion DMA-request path.

## Confidence boundary

The corpus verifies the CPU-visible byte protocol, controller phases, status,
DMA completion, image reads/writes, and original software compatibility. Flat
sector images contain no flux timing, index position, gap bytes, CRC fields,
deleted marks, or S1410 bad/alternate-track headers. Those physical-medium
details need an indexed track/flux backend and hardware timing measurements;
they are not claimed exact by the present tests.

# Partner SASI adapter

Canonical source:
[iskra-delta/IskraDeltaPartnerSASIAdapter](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter)

Pinned revision: [`d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/tree/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e),
committed 2023-08-23.

The repository identifies the reverse-engineered assembly as SASI adapter
`046 700 134`.

## KiCad entry point and sheets

Open
[`SASI_AD/SASI_AD.kicad_pro`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/SASI_AD.kicad_pro).
Its root sheet is
[`SASI_AD.kicad_sch`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/SASI_AD.kicad_sch),
with these hierarchical sheets:

- [`BUS.kicad_sch`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/BUS.kicad_sch)
- [`SASI.kicad_sch`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/SASI.kicad_sch)
- [`control.kicad_sch`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/control.kicad_sch)
- [`powers.kicad_sch`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/powers.kicad_sch)

Keep `DS8838.kicad_sym` and `sym-lib-table` with the project. A ready-to-read
PDF export is available as
[`SASI_AD_white.pdf`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/blob/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e/SASI_AD/SASI_AD_white.pdf).

## Emulator relevance

Use these sheets to verify the Partner bus interface, SASI data transceivers,
control/status polarity, port decode, handshake ordering, resets, and cable
signals modeled by the Xebec/SASI path. The behavioral companion note is
[`SASI-XEBEC-HANDSHAKE.md`](../patterns/SASI-XEBEC-HANDSHAKE.md).

## Connections verified against the emulator

- The adapter is selected throughout `10h..1fh`; A2 and A3 are ignored and
  A1:A0 select the four local functions.
- Status is D7 `REQ`, D6 `IO`, D5 `MSG`, D4 `CD`, and D3 `BSY`.
- Function 2 reads and the unused function 3 read float high as `ffh`.
- REQ/DACK/DMARQ transitions are sampled once per CPU or DMA bus cycle so one
  stretched I/O access cannot consume multiple bytes.

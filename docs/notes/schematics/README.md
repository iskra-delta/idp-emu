# Partner schematic sources

This directory records the maintained, reverse-engineered Partner hardware
sources. The upstream KiCad repositories are the primary reference; the older
scans in [`docs/vendor`](../../vendor/README.md) remain useful for provenance
and comparison.

The revisions below were checked on 2026-08-26. Links to a commit are stable;
links to `master` are convenient for checking later corrections.

| Assembly | Local note | Upstream | Pinned revision |
| --- | --- | --- | --- |
| Partner 40 motherboard | [`MOTHERBOARD.md`](MOTHERBOARD.md) | [Partner40](https://github.com/iskra-delta/Partner40) | [`75b2822`](https://github.com/iskra-delta/Partner40/tree/75b28222770551c5fbbf8bfb3a2a273d7f73f817) |
| SASI adapter 046 700 134 | [`SASI-ADAPTER.md`](SASI-ADAPTER.md) | [IskraDeltaPartnerSASIAdapter](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter) | [`d1a6d95`](https://github.com/iskra-delta/IskraDeltaPartnerSASIAdapter/tree/d1a6d958f2f9db793e3e8dc4c99d2a84ecacb86e) |
| Partner P CRT text-video board 30 797 044 | [`CRT-TEXT-VIDEO.md`](CRT-TEXT-VIDEO.md) | [IskraDeltaPartnerVideo](https://github.com/iskra-delta/IskraDeltaPartnerVideo) | [`89c085b`](https://github.com/iskra-delta/IskraDeltaPartnerVideo/tree/89c085b3157072d1eb21bbf89ac6397db43bdba4) |
| Partner G GDP card | [`GDP-CARD.md`](GDP-CARD.md) | [PartnerGDP](https://github.com/iskra-delta/PartnerGDP) | [`140c852`](https://github.com/iskra-delta/PartnerGDP/tree/140c852cf9238c5dc868f3c959167f4083d93dbc) |

## Using the sources

Clone the relevant repository and check out the pinned revision before using
it as evidence for emulator behavior. Open the top-level `.kicad_pro` named in
the corresponding note; do not open a child sheet as an independent project.
Keep every hierarchical `.kicad_sch`, custom symbol library, and library table
beside the project file.

These are reverse-engineered reconstructions, not factory design masters.
Where a reconstruction, an original drawing, and physical hardware disagree,
record the discrepancy and prefer a reproducible hardware measurement before
changing the emulator.

# Pattern: ROM Assembly Annotation Formatting

Category: Documentation / ROM Disassembly / Style Guide  
Date(s): 2026-03-21

## Problem / Purpose

Keep annotated ROM disassembly files consistent, readable, and diff-friendly.

This document defines the canonical formatting rules for:

- `docs/roms/rom-crt-anno.txt`
- `docs/roms/rom-gdp-anno.txt`

## Canonical Code Line Format

Instruction/data lines use fixed columns and spaces only:

```asm
AAAA  bb bb bb bb   mnemo  operands                      ; comment
```

Where:

- `AAAA` = 4-digit uppercase hex address (no trailing `:`)
- byte stream = lowercase hex bytes
- mnemonic = lowercase (`ld`, `jp`, `call`, `ret`, ...)
- operands = lowercase registers/conditions/symbols
- comments start at one common aligned column
- tabs are never used

## Numeric Style Rules

In code operands:

- Hex uses `0x` prefix, never `h` suffix
  - Correct: `0x0218`
  - Wrong: `0218h`
- Immediate/direct numeric operands use `#`
  - Correct: `ld    hl,#0x0218`
  - Correct: `ld    sp,#0xffc0`
- Memory/indirect addressing uses parentheses
  - Correct: `ld    a,(0xd9)`
  - Correct: `out   (0xc8),a`

Notes:

- Labels keep their existing suffix style (`name_022e`), this rule applies to code operands.
- Comments may contain historical notation, but code fields must follow these rules.

## Label and Comment Block Spacing Rules

### 1) Main routine block (section comment + label)

Keep section comments and label together, with no blank line between them:

```asm
; --------------------------------------------------
; 0066: unused jump
; jumps to invalid command handler, never referenced
unused_jump_0066:
0066  c3 1d 00        jp    invalid_command_001d         ; redundant jump to '?' output
```

### 2) Local label without local comment

Insert one empty line before the local label:

```asm

print_nibble_009b:
009B  cd ac 00        call  gdp_command_00ac             ; output ascii char to gdp
009E  c9              ret
```

### 3) Local label with local comment block

Insert one empty line before the local comment block, and keep comment + label contiguous:

```asm
008B  cd 90 00        call  print_hex_nibble_0090        ; print low nibble as ascii
008E  c1              pop   bc                           ; restore bc
008F  c9              ret                                ; return to caller

; 0090: unused nibble-to-ascii conversion
; converts a nibble (0-f) to ascii and prints it
print_hex_nibble_0090:
0090  fe 0a           cp    #0x0a                        ; compare with 10 (digit or letter?)
0092  fa 99 00        jp    m,nibble_is_digit_0099       ; if < 10, handle as 0-9
```

## Naming Rules

- Labels must be lowercase snake_case with address suffix:
  - `after_fdc_reset_022e`
  - `print_hex_nibble_0090`
- Do not use `*_0xNNNN` label variants.

## Consistency Requirements

- Both `rom-crt-anno.txt` and `rom-gdp-anno.txt` must follow the same formatting rules.
- Use only spaces (no tabs).
- Keep comment alignment consistent globally.
- Keep section headers, local comment blocks, and labels structurally stable to minimize noisy diffs.

## Quick Checklist

Before finishing edits to annotated ROM files:

1. No tabs in file.
2. No `AAAA:` instruction address style (must be `AAAA`).
3. No `h`-suffixed hex in code operands.
4. Immediate numeric operands are `#0x...`.
5. Label/comment spacing follows rules 1–3 above.
6. Comment column alignment is consistent.


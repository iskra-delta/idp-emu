# Z80 Assembly Style Guide

Style conventions extracted from the partos source files (`src/**/*.s`), assembled with **sdas** (SDCC assembler).

---

## File Header

Every `.s` file begins with a header block at the standard 12-space indent:

```asm
            ;; filename.s
            ;;
            ;; brief one-line description
            ;;
            ;; YYYY-MM-DD   initials
            .module modulename
```

- Filename on the first line, then a blank `;;`, description, blank `;;`, date + author initials.
- `.module` declaration follows immediately, with no blank line between the header and the directive.
- All header lines — including the `;;` lines — are indented to **column 12**.

---

## Column Layout

All non-label, non-blank lines (instructions, directives, comments) start at **column 12** (12 spaces).

| Column | Content |
|--------|---------|
| 0      | Labels only (`name::` or `name$:`) |
| 12     | Mnemonic / directive / `;;` comment |
| 20     | Operands (mnemonic is padded to 8 characters) |
| 41    | Inline `;` comment |

Example:

```asm
            xor     a
            ld      (ir_refcnt),a
            ei
            ret
```

```asm
la_fail$:   cp      #0xff               ; this will reset Z flag
```

When a local label shares the line with an instruction the label fills columns 0–11 and the instruction continues from column 12 as normal.

---

## Indentation

- **Spaces only** — no tabs in instruction lines.
- 12-space indent is applied universally to instructions, directives, and comment-only lines.
- Labels are never indented.

---

## Casing

| Element | Case | Example |
|---------|------|---------|
| Mnemonics | lowercase | `ld`, `xor`, `djnz`, `jr` |
| Registers | lowercase | `a`, `hl`, `bc`, `de`, `b` |
| Condition codes | lowercase | `nz`, `nc`, `z` |
| Assembler directives | lowercase | `.module`, `.area`, `.globl`, `.ds` |
| Area names | UPPERCASE | `_CODE`, `_SYSVARS`, `_INTVEC` |
| Public routine names | lowercase snake\_case | `ir_init`, `delay_1ms`, `lock_acquire` |
| Local labels | lowercase snake\_case + `$` | `la_fail$`, `d1m_loop$`, `ire_ei$` |

---

## Labels

- **Public symbols** use double-colon `::`. This exports the symbol globally without requiring a separate `.globl` line.
- **Local labels** carry a `$` suffix and are only referenced within their routine. They follow the same `name$:` form.
- Use `.globl` only when exporting a symbol defined in another module that is consumed by this one.

```asm
lock_acquire::          ; public, exported
            ...
la_fail$:               ; local, private to this routine
            ...
```

---

## Routine Header (Section Divider)

Every public routine is preceded by a three-part block:

```asm
            ;; ----------------------------------------------------------------
            ;; <return> result <= routine_name(<type> arg, ...)
            ;; ----------------------------------------------------------------
            ;; description of what the routine does.
            ;;
            ;; input(s):
            ;;  reg  ... description or value
            ;; output(s):
            ;;  reg  ... description or value
            ;; destroys:
            ;;  reg  ... value left in register (use 0 if zeroed)
            ;;  flags
            ;; ----------------------------------------------------------------
routine_name::
```

Rules:
- The divider is 64 dashes, padded to column 12 like every other comment line.
- The signature line uses C-influenced pseudo-code: `<type> name` for return value on the left, `(<type> reg)` for inputs in parentheses.
- Sections present only when applicable: `input(s)`, `output(s)`, `destroys`. Omit a section entirely if it does not apply.
- Register entries use the format `reg   ... description`, where `reg` is padded with spaces and `...` is a literal separator.
- When a register is zeroed by the routine, write `0` as the value.
- `flags` appears under `destroys` with no `...` description when the routine clobbers the flag register.

### Examples

Simple routine, no inputs or outputs (from `ir.s`):

```asm
            ;; ----------------------------------------------------------------
            ;; ir_disable()
            ;; ----------------------------------------------------------------
            ;; execute di instruction with reference counting
            ;;
            ;; destroys:
            ;;  flags
            ;; ----------------------------------------------------------------
ir_disable::
```

Full form with result, inputs, outputs and destroys (from `lock.s`).
Flag results are documented under `output(s)` as `flags   ... <meaning>`:

```asm
            ;; ----------------------------------------------------------------
            ;; <z flag> result <= lock_acquire(<hl> *lock)
            ;; ----------------------------------------------------------------
            ;; tries to acquire a lock using atomic sra (hl) instruction. if
            ;; successfull it sets the z flag.
            ;;
            ;; input(s):
            ;;  hl  ... pointer to memory location holding the lock
            ;; output(s):
            ;;  flags   ... Z is set on success
            ;; destroys:
            ;;  a   ... 0
            ;; ----------------------------------------------------------------
lock_acquire::
```

Extended discussion (derivations, timing analysis) belongs in the
description part, between the signature divider and the register
sections, as numbered or bulleted `;;` lines (from `delay.s`):

```asm
            ;; ----------------------------------------------------------------
            ;; delay_1ms()
            ;; ----------------------------------------------------------------
            ;; 1ms very accurate delay. it also accounts for the call to this
            ;; function. it should be called with interrupts disabled.
            ;;
            ;; the timing for this code is:
            ;; static code
            ;;   1. init call 17 t-states
            ;;   2. ret = 10 t-states
            ;;   ...
            ;;
            ;; destroys:
            ;;  b   ... 0
            ;;  hl  ... hl-209
            ;;  flags
            ;; ----------------------------------------------------------------
delay_1ms::
```

- A routine that returns nothing and takes no arguments is written as
  `name()` — no `<return> result <=` part and no semicolon.
- When a destroyed register holds a derived value, document the value
  expression (e.g. `hl  ... hl-209`).

---

## Inline Comments

- Single `;` for end-of-line comments, aligned at **column ~36**.
- Double `;;` for standalone comment lines (file header, routine headers, section notes).
- Never mix `;;` and `;` on the same line.

Timing annotations in cycle-counting contexts use the t-state count directly as the comment:

```asm
            ld      b,#209              ; 7
d1m_loop$:
            dec     hl                  ; 6
            djnz    d1m_loop$           ; 13/8
            ret                         ; 10
```

---

## Immediate Values

SDAS requires `#` prefix for immediate operands:

```asm
            ld      b,#209
            cp      #0xff
            ld      bc,#2048
```

- Decimal: plain digits — `#209`, `#2048`.
- Hexadecimal: `#0x` prefix, lowercase digits — `#0xff`, `#0xfe`.

---

## Memory Areas

Declare areas with `.area` in UPPERCASE:

```asm
            .area   _CODE
            .area   _LOADER
            .area   _KERNEL
            .area   _INTVEC
            .area   _SYSVARS
```

Group variable declarations at the end of the file under `.area _SYSVARS`:

```asm
            .area   _SYSVARS
            ;; pointer to ir reference count
ir_refcnt::
            .ds     1
```

---

## Commented-Out Code

Prefix with `;;` (not `;`), consistent with other standalone comment lines:

```asm
            ;;ld      de,#_KERNEL_ADDR
```

No space between `;;` and the disabled instruction.

## Common Routines

They should be thread safe, meaning that all data should
be in registers, alt. registers or on the stack. Unless
there is a shared (singleton) memory structure!
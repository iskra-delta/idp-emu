# x compiler suite — bug report (found porting PartOS)

Found while migrating the PartOS Z80 build (SDCC → x suite) on 2026-07-01.

**Toolchain under test** — docker image `wischner/xcc-z80:latest`
(`= wischner/xcc-z80:1.9.1`, `sha256:3ce129951cde…`):

- `xcc 1.9.1`
- `xas 0.1.0`
- `xld 1.0.0`

**How to reproduce anything below** — run inside the image:

```bash
docker run --rm -v "$PWD:/work" -w /work wischner/xcc-z80:latest bash
# then paste the repro for a given bug
```

The PartOS sources are hand-written `sdasz80` assembly + C compiled with
`sdcc --sdcccall 1`. xcc advertises the same `sdcccall(1)` ABI and xas
advertises `sdasz80` syntax, so these are compatibility gaps against that
baseline. Bugs are ordered by severity.

## Status — re-verified against `wischner/xcc-z80:1.9.2` (xcc 1.9.2)

| Bug | 1.9.1 | 1.9.2 |
|---|---|---|
| 1 — `-O2/-O3/-Os/-Of` drop digits | broken | **FIXED** (all levels print `2304 10752 10240`) |
| 3 — `label::` not exported | broken | **FIXED** |
| 4 — `ret c` rejected | broken | **FIXED** |
| 5 — `==` equate rejected | broken | **FIXED** |
| 6 — cross-module `.equ`/`=` relocated | broken | **FIXED** (links to `CD 34 12`) |
| 7 — named label-difference constant | broken | **FIXED** (`ld de,#5`) |
| 8 — no macros | N/A | not a bug (author: macros present) |
| 2 — stack-arg cleanup side | broken | **STILL BROKEN** — now confirmed + isolated (below) |

---

## BUG 1 — xcc: `-O2`/`-O3`/`-Os`/`-Of` miscompile a 32-bit compare/subtract loop (drops digits) — CRITICAL

`xcc` at optimization level `-O2` and above (including `-Os` and `-Of`)
generates wrong code for a loop that repeatedly compares and subtracts a
32-bit (`unsigned long`) value against elements of a `const uint32_t[]`
table. Decimal digits that should be produced are silently dropped.
`-O0` and `-O1` are correct.

### Minimal reproducer

`fmt.c`:

```c
#include <stdio.h>
typedef unsigned long uint32_t;
typedef unsigned char uint8_t;

static void fmt(uint32_t value, char *dst) {
    static const uint32_t powers[10] = {
        1000000000UL,100000000UL,10000000UL,1000000UL,100000UL,
        10000UL,1000UL,100UL,10UL,1UL};
    uint8_t started = 0, i;
    for (i = 0; i != 10; ++i) {
        uint8_t digit = 0;
        while (value >= powers[i]) { value -= powers[i]; digit++; }
        if (digit || started || i == 9) { *dst++ = (char)('0' + digit); started = 1; }
    }
    *dst = 0;
}

int main(void) {
    static const uint32_t tv[3] = {2304, 10752, 10240};
    char b[16]; int k;
    for (k = 0; k < 3; k++) { fmt(tv[k], b); puts(b); }
    return 0;
}
```

Build + run in the suite's own emulator:

```bash
for O in O0 O1 O2 O3 Os Of; do
  xcc --platform=emu -$O fmt.c --oformat=ihx -o f.ihx
  printf '%-3s: ' "-$O"
  xemu --run --emu-stdio --max-steps 5000000 --load-ihx f.ihx | tr '\n' ' '; echo
done
```

### Expected

Every level prints `2304  10752  10240`.

### Actual

```
-O0: 2304 10752 10240      <-- correct
-O1: 2304 10752 10240      <-- correct
-O2: 234 1752 1240         <-- WRONG (interior '0' digits dropped)
-O3: 234 1752 1240         <-- WRONG
-Os: 234 1752 1240         <-- WRONG
-Of: 234 1752 1240         <-- WRONG
```

Note the pattern: `2304→234`, `10752→1752`, `10240→1240` — the loop that
should emit a `0` digit (i.e. `while (value >= powers[i])` executes zero
times but a digit column is still due) instead skips the column. This looks
like a mis-optimization of the 32-bit `>=` compare and/or the `digit`/`started`
bookkeeping when the inner `while` body runs zero times.

### Impact

This is the headline blocker: it also appears to cause hard hangs in larger
programs at `-O2+/-Os` (loops that never terminate), so real code cannot use
the size/speed optimization levels. Only `-O0`/`-O1` are usable, and `-O1` is
actually smaller than `-O0` here (1584 vs 1742 bytes for this TU), so the size
optimizer is both wrong and unnecessary for small wins — but everyone will
reach for `-Os` first.

### Workaround

Use `-O1`.

---

## BUG 2 — xcc `sdcccall(1)` cleans stack-passed arguments on the CALLER side; sdcc cleans them on the CALLEE side — HIGH, **CONFIRMED (1.9.2)**

### The confirmed defect

For a function whose 3rd+ argument is passed on the stack, xcc emits
**caller-side** cleanup after the call, whereas `sdcc --sdcccall 1` emits
**none** (the callee owns the cleanup). Same source, `_app_read_file`
call site:

```
# sdcc -mz80 --sdcccall 1 -S        # xcc -S -O1
    call _app_read_file                 call _app_read_file
    ld   a, d          <-- no cleanup   inc  sp     <-- caller frees the
    or   a, e                           inc  sp         stacked arg (add sp,#2)
```

So when xcc-compiled C calls a hand-written `sdcccall(1)` asm bridge that
frees the stacked arg itself (the PartOS FAT/service bridges do:
`app_ret_clean2$: pop hl; pop bc; jp (hl)`), the argument is **freed twice** →
`SP` drifts `+2` per call.

### Isolated, self-contained reproducer (runs in xemu)

`drip.s` — an `sdcccall(1)` callee that takes a 3rd stacked arg and callee-cleans
it (exactly like the PartOS bridges):

```asm
            .globl _drip
            .area _CODE
_drip::                         ; hl=a, de=b, stack=[ret][c]
            ld      hl,#2
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)      ; bc = c
            pop     hl          ; ret addr
            pop     de          ; discard stacked c  <-- CALLEE cleanup
            push    hl
            ld      h,b
            ld      l,c         ; hl = return value (c)
            ret
```

`main.c`:

```c
#include <stdio.h>
extern int drip(int a, int b, int c);
int main(void){ int i; long acc=0;
    for (i=0;i<4000;++i) acc += drip(1,2,3);   /* SP drift accumulates */
    puts(acc==12000 ? "OK" : "BAD"); return 0; }
```

```bash
for O in O0 O1 Os; do
  xcc --platform=emu -$O main.c drip.s --oformat=ihx -o m.ihx
  printf '%-3s: ' "-$O"; xemu --run --emu-stdio --max-steps 40000000 --load-ihx m.ihx
done
```

Result on 1.9.2: **`-O0` → (crash/no output), `-O1` → (crash/no output),
`-Os` → `BAD`.** Control: replace `drip.s` with a pure-C `int drip(int a,int
b,int c){return c;}` → **`OK` at every level** (xcc caller-cleanup matches an
xcc callee, so no drift). That isolates the trigger to the sdcc-style
callee-cleanup boundary.

### Real-world impact in PartOS (xcc 1.9.2)

Every tool reaches these bridges through the shared C runtime, so the whole
userland is affected — the shell regression scores **0/19 at `-Os`** and
**8/19 at `-O1`**. It is *worse* at `-Os` because xcc's IX frame-pointer
epilogue (`ld sp,ix / pop ix / ret`) resets `SP` at each function return and
therefore **masks** the drift for functions that make only a handful of bridge
calls; at `-O1` those few-call tools (`ls`, `echo`, `mkdir`, `cd`) survive and
only the loop-heavy `cp`/`mv` accumulate enough drift to fail. At `-Os` that
masking no longer saves them.

### Suggested fix

Match `sdcc --sdcccall 1`: **the callee cleans stack-passed arguments**;
callers must emit **no** post-call `SP` adjustment. (Equivalently, if xcc wants
caller-cleanup as its own ABI, it is then not `sdcccall(1)`-compatible for any
hand-written asm that follows the sdcc convention.)

### Historical note (original under-specified symptom)

Symptom at `-O1` (BUG 1 avoided): the two file-copy tools misbehave — `cp`
aborts with its generic error `?` after creating a truncated/garbage file, and
`mv` **never terminates** (still spinning at 60M instructions). Every *other*
tool works (`ls`, `echo`, `mkdir`, `cd`, `rmdir`, `del`, …).

### Why this is almost certainly NOT a plain loop/formatter codegen bug

- The loop counter is fine: `cp_secs` (a `static uint16_t`) is assigned from
  `app_file_sector_count()` *before* the loop and counted down with
  `cp_secs--`; the same shape works elsewhere.
- It is **not** a struct-layout/ABI offset problem: `cp`/`mv` block on
  `app_wait_status(&file.status)` where `.status` is at offset 10 of
  `fat_file_t`. `mkdir`/`cd` block on the identical offset-10 `.status` of
  `fat_dirent_t` and work fine, so xcc lays those fields out correctly.

### The actual discriminator

`cp`/`mv` are the **only** tools that call the hand-written FAT bridges in
`src/apps/lib/fscall.s` **inside a loop**. Those bridges implement the
`sdcccall(1)` boundary for 3-argument OS calls where the **3rd argument is
passed on the stack**, and they perform **callee-side cleanup** of that
stacked argument. See `app_file_common$` (used by `app_read_file` /
`app_write_file`) and its shared return tail:

```asm
;; int16_t app_read_file(fat_file_t *file, void *buf, uint16_t bytes)
;;   in: hl=file, de=buf, stack=[ret][bytes]     <-- 3rd arg on the stack
app_file_common$:
    ...
    jp   app_ret_clean2$

app_ret_clean2$:
    pop  hl        ; caller return address
    pop  bc        ; drop the ONE stacked argument  <-- CALLEE cleans the stack
    jp   (hl)
```

This callee-cleans-the-stack-arg contract is what SDCC `--sdcccall 1` produced
(these bridges have always worked under sdcc). xcc, by contrast, gives every
function an **IX frame pointer** and restores `SP` from it at the epilogue
(`push ix / ld ix,#0 / add ix,sp` … `ld sp,ix / pop ix / ret`, visible in
`xcc -S`). That epilogue **masks any per-call SP imbalance** for a one-shot
call (mkdir/cd call the structurally identical `app_path_common$` exactly once
and survive), but `cp`/`mv` call `app_read_file`+`app_write_file` ~42 times in
a single function body, so any per-call `SP` drift **accumulates across the
loop** until the stack is corrupted → `cp` returns into garbage / bails, `mv`
spins forever.

### Primary hypothesis

xcc’s `sdcccall(1)` disagrees with sdcc on **stack-passed arguments** — most
likely it emits **caller-side** cleanup (an `add sp,#n` / `pop` after the
`call`) while sdcc (and therefore these bridges) expect **callee-side**
cleanup. Two cleanups per call ⇒ `SP` drifts a fixed amount each iteration.
(A less likely variant: a register the bridge assumes is preserved/passed —
e.g. the IX frame pointer, or the 3rd arg’s stack slot offset — differs.)

### Fastest confirmation (no new reproducer needed)

Compile one caller and look at the call site:

```bash
xcc -S -O1 -I partos/src/apps/lib partos/src/apps/cp/cp.c -o - | sed -n '/call\s*_app_read_file/,+3p'
```

If there is an `add sp,#2` / `pop` **after** `call _app_read_file` (or
`_app_write_file`), that is the caller-side cleanup that double-frees the
stacked arg against `app_ret_clean2$` — confirmed. Compare with sdcc’s output
for the same file: sdcc should emit **no** stack cleanup there.

### Suggested standalone reproducer (to add as a regression)

An asm `sdcccall(1)` callee that takes a 3rd stacked arg and callee-cleans it,
called in a long loop from C, with an SP canary:

```asm
; drip.s  — int drip(int a,int b,int c): returns c; CALLEE-cleans stacked c
    .globl _drip
    .area  _CODE
_drip:                 ; hl=a, de=b, stack=[ret][c]
    ld   hl,#2
    add  hl,sp
    ld   c,(hl)
    inc  hl
    ld   b,(hl)        ; bc = c
    pop  hl            ; ret addr
    pop  de            ; drop stacked c   (callee cleanup)
    ld   d,b
    ld   e,c
    ex   de,hl         ; return value in hl? (set per your sdcccall(1) ret reg)
    push de            ; keep ret addr... (finalize per ABI)
    ret
```

```c
extern int drip(int a, int b, int c);
int main(void){
    int i, acc = 0;
    for (i = 0; i < 2000; ++i) acc += drip(1, 2, 3);  /* SP drift => crash/hang */
    puts(acc == 6000 ? "OK" : "BAD");
    return 0;
}
```

Expected `OK`; a caller/callee cleanup mismatch makes this crash, hang, or
print `BAD`. (I did not run this — the image under test predates your fixes.)

### Note / caveat

Separately, my migration’s `crt0.s` is intentionally minimal and does **not**
zero BSS (it assumes xcc’s `_DATA` bytes are carried in the loaded image). That
is unrelated to cp/mv (they populate their file/handle state before use), but
it may explain a *different* observation — apps behaving erratically at `-O0`
(e.g. `ls` failing there) where the working set lands on non-zeroed RAM. Worth
keeping separate from this bug.

---

## BUG 3 — xas: `label::` (sdas global-export) is silently NOT exported — HIGH

In `sdasz80`, a label defined with a double colon (`foo::`) is a **global
export** (equivalent to `.globl foo` + `foo:`). `xas` accepts the syntax but
emits **no symbol at all** for it, so cross-module links fail with
`unresolved symbol` unless every such label also has an explicit `.globl`.

### Minimal reproducer

```bash
printf '            .area _CODE\nfoo::\n            ret\n' > g.s
xas -o g.rel g.s
grep -c 'foo' g.rel     # -> 0  (foo is not in the object at all)

printf '            .globl foo2\n            .area _CODE\nfoo2::\n            ret\n' > h.s
xas -o h.rel h.s
grep 'foo2' h.rel       # -> "S foo2 Def00000000"  (only works with explicit .globl)
```

### Expected

`foo::` exports `foo` as a global symbol (as sdasz80 does).

### Actual

`foo::` exports nothing; an explicit `.globl foo` is required.

### Workaround

Add `.globl <name>` for every `<name>::`.

---

## BUG 4 — xas: `ret c` rejected ("unrecognised RET form") — HIGH

The conditional return **`RET C`** (return if carry, opcode `0xD8`) is
rejected. Every *other* conditional return assembles fine — the parser appears
to confuse the condition code `c` with register `C`.

### Minimal reproducer

```bash
for f in "ret c" "ret nc" "ret z" "ret nz" "ret m" "ret p" "ret pe" "ret po"; do
  printf '            .area _CODE\n_t:\n            %s\n' "$f" > t.s
  if xas -o t.rel t.s 2>/dev/null; then echo "OK   : $f"; else echo "FAIL : $f"; fi
done
```

### Expected

All eight conditional returns assemble.

### Actual

```
FAIL : ret c
OK   : ret nc
OK   : ret z
OK   : ret nz
OK   : ret m
OK   : ret p
OK   : ret pe
OK   : ret po
```

Error text: `error: unrecognised RET form`.

### Workaround

`.db 0xd8   ; ret c`

---

## BUG 5 — xas: `==` (sdas absolute-global equate) rejected — HIGH

`sdasz80` uses `name == expr` to define a **global absolute** symbol (and
`name = expr` for a local one). `xas` rejects `==` outright.

### Minimal reproducer

```bash
printf '            .area _CODE\nK == 5\n            ld de,#K\n' > e.s
xas -o e.rel e.s
```

### Expected

`K` is defined as the absolute constant `5`.

### Actual

`error: unexpected token in expression: '='`

### Workaround

`.equ K, 5` (but see BUG 6/7 for the cases where that is *also* wrong).

---

## BUG 6 — xas/xld: `=`/`.equ` symbols are relocated, not absolute constants — HIGH

A symbol defined with `=` or `.equ` to a **literal value** and then referenced
from **another module** is treated as area-relative and **relocated** by the
linker (it comes out as `defining_area_base + value`) instead of being a fixed
absolute constant.

### Minimal reproducer

`imports.s` (defines the "constant"):

```asm
            .globl kfunc
            .equ   kfunc, 0x1234
```

`os.s` (references it from another module):

```asm
            .globl _start
            .globl kfunc
            .area  _CODE
_start:     call kfunc
            ret
```

```bash
xas -o imports.rel imports.s
xas -o os.rel os.s
xld -nostdlib -e _start --oformat=ihx \
    --section-start=_CODE=0xC000 os.rel imports.rel -o out.ihx
head -1 out.ihx     # decode the CALL operand
```

### Expected

`call kfunc` assembles/links to `CD 34 12` (call `0x1234`).

### Actual

Links to `CD 7C 16` etc. — the value `0x1234` was **relocated** by the OS
module's area base (`0xC000 + code_offset + 0x1234`, truncated to 16 bits).
i.e. `.equ`/`=` do not produce link-time absolute constants across modules.

### Impact

This removes the only portable way to hand a separately-linked module a set of
fixed addresses (our kernel→OS ABI-import stub relied on sdas `name == addr`).
There is currently **no** working mechanism: `==` (BUG 5), `=`/`.equ` (this
bug), and `.area (ABS)` + `.org` / `--section-start` were all tried and all
either error or relocate.

### Suggested fix

Honor sdas semantics: `name == expr` and `name = expr`/`.equ` define **absolute
(non-relocatable) symbols**; references to them emit fixed immediates and are
not entered into the relocation table.

---

## BUG 7 — xas: a named label-difference constant fails to resolve — HIGH

A named symbol whose value is a **label-difference expression** in the same
area (a very common sdas idiom for string lengths, e.g.
`msg_len == . - msg`) does not survive to link: the reference comes out as an
**unresolved external**. The *inline* form of the same expression works, which
shows xas can compute label differences — it just can't carry them as a named
symbol.

### Minimal reproducer

```bash
# (a) named label-difference constant -> FAILS
printf '            .globl _start\n            .area _CODE\nmsg:  .db 1,2,3,4,5\n            .equ msg_len, . - msg\n_start:  ld de,#msg_len\n         ret\n' > a.s
xas -o a.rel a.s && xld -nostdlib -e _start --oformat=ihx --section-start=_CODE=0x9000 a.rel -o a.ihx
#   -> xld: error: unresolved symbol 'msg_len'

# (b) same value inline -> WORKS
printf '            .globl _start\n            .area _CODE\n_start:  ld de,#(msg_end - msg)\n         ret\nmsg:  .db 1,2,3,4,5\nmsg_end:\n' > b.s
xas -o b.rel b.s && xld -nostdlib -e _start --oformat=ihx --section-start=_CODE=0x9000 b.rel -o b.ihx
head -1 b.ihx     # -> ...11 05 00...  (ld de,#5, correct)
```

### Expected

`(a)` links with `ld de,#5` just like `(b)`.

### Actual

`(a)` → `xld: error: unresolved symbol 'msg_len'`. `(b)` is correct.

### Workaround

Inline the difference at the use site (`#(end - start)`), or precompute a
literal.

---

## BUG 9 — xcc `-Os`: `(n == 1)` guard of a chained `&&` folded to `(n != 0)` — HIGH, **found on 1.9.3**

Found on 2026-07-01 with `wischner/xcc-z80:1.9.3` while getting `cd ..` to work
in PartOS. The path canonicaliser tests path segments like this:

```c
if ((seg_len == 1u) && (segment[0] == '.'))  { /* "."  */ ... }
if ((seg_len == 2u) && (segment[0] == '.') && (segment[1] == '.')) { /* ".." */ ... }
```

At `-Os` the first `if` matched for `seg_len == 2` too, so `".."` was treated as
`"."` and never popped — `cd ..` silently stayed in the sub-directory. gcc `-O2`
and xcc `-O0` both compile it correctly; only xcc `-Os` is wrong.

### Minimal reproducer (`classify(2, "..")` must return `2`, returns `1`)

```c
unsigned char classify(unsigned char n, const char *s)
{
    if ((n == 1u) && (s[0] == '.')) {
        return 1;                     /* single dot  "."  */
    }
    if ((n == 2u) && (s[0] == '.') && (s[1] == '.')) {
        return 2;                     /* double dot  ".." */
    }
    return 0;
}
```

```bash
xcc --sdcccall 1 -Os -S classify.c -o classify.s   # buggy
xcc --sdcccall 1 -O0 -S classify.c -o classify.s   # correct
```

### What the codegen shows

`-O0` (correct) tests `n == 1` as a real compare (`ld hl,#1; sbc hl,de`):

```
	ld	hl, #1
	pop	de
	or	a, a
	sbc	hl, de        ; n - 1  -> zero only when n == 1
```

`-Os` (buggy) drops the `== 1` entirely and only tests `n != 0`:

```
	ld	l, a
	ld	h, #0
	or	a, l          ; <-- tests n != 0, NOT n == 1
	jr	z, __xcc_L2
__xcc_L3:
	... ld a,(s[0]); cp #46; jr nz ...
__xcc_L0:
	ld	a, #1         ; returns 1 for ANY n != 0 with s[0]=='.'
```

The optimizer appears to weaken `n == 1` into `n != 0` when a *sibling* `if`
downstream also narrows `n` (here the `n == 2` test). Reproduces with a
`const char *` argument at `-Os`.

### Source workaround used in PartOS

Test the more specific `".."` (`seg_len == 2`) branch **before** the `"."`
(`seg_len == 1`) branch, so the correctly-compiled `== 2` guard consumes the
segment before the weakened `== 1` guard can match. See
`partos/src/apps/lib/partos_c.c:app_resolve_path`.

---

## BUG 8 — xas: no assembler macros — MINOR / documented

`xas --help` states "Supported source subset excludes assembler macros".
`sdasz80` supports `.macro`/`.endm`; code using them won't assemble. Noting it
for completeness (it's a documented limitation, not a crash).

---

## Notes / suggested priorities

1. **BUG 1** (`-O2+/-Os` codegen) is the showstopper — nothing real can use the
   optimizer. Likely also the root of BUG 2's hangs.
2. **BUG 6** (no absolute cross-module constants) blocks any multi-image ABI
   that isn't fully dynamic.
3. **BUGs 3/4/5/7** are smaller sdas-compat parser gaps; each has a mechanical
   source workaround, but fixing them in xas would let a large existing sdas
   codebase assemble unchanged.
4. **BUG 9** (`-Os` folds `== 1` into `!= 0`) is a correctness bug in the
   optimizer's condition narrowing — subtle because it only bites when a sibling
   `if` further downstream also constrains the same variable. Has an easy source
   workaround (order specific branch first) but is a real miscompile.

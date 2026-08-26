# PartOS Problems: FDD `partos_kernel_boot` derail #2 (worker-stack corruption)

## RESOLVED (2026-06-20)
FDD path now PASSES end-to-end (`partos_kernel_boot` depth 10/10, loader_reached;
`partos_full_boot` HDD gate stays green). Root cause was NOT on the worker — it
was a **stack imbalance in `fat_file_submit$`** (the shared `_fat_read`/`_fat_write`
submit path, `partos/src/os/fat_file.s`), which runs on the BOOTSTRAP thread:
`fat_evt_reset$` (via `_evt_set`) clobbers `hl`(file)/`de`(buf) and returns
`de=event`; the old code then `push de`/`push hl`'d those CLOBBERED values
WITHOUT a matching pop, leaking +2 words. At the final `jp fat_ret_clean4$`
(E562) the SP was 4 bytes too deep, so clean4's `pop hl` grabbed the stranded
`push de` value (= the event handle FB43) and `jp (hl)` jumped into the heap ->
NOP-slide -> derail at 01C3. It surfaced FDD-only/now because the read submit is
only reached after mount+open complete. Fix: rewrote `fat_file_submit$` to save
buf/file BEFORE the clobbering calls, with balanced [op][file][buf] frame cleanup
(also fixed the `file->status=EBUSY` write that used a garbage pointer). Plus two
follow-on fixes: `KERNEL_LOAD_BASE` 0xBEC0->0xBEC1 (code grew 3 B; must equal the
linked `_CODE` base), and the probe's `PROCESS_LOAD_IMG` symbol index 2->1
(idx 2 is `_process_exit` post-refactor; load_image is idx 1) so loader_reached
is detected and the catcher doesn't false-fire on the legitimately-loaded shell.

## TL;DR (historical, derail #2 below is now fixed)
The PartOS kernel boots fully on **HDD/FAT16** (`partos_full_boot` PASSES the
complete mount→open→read→shell-load path). On the **FDD/FAT12** path
(`partos_kernel_boot`), after the mount completes the worker **derails** during
`fat_open`/lookup completion: `fat_complete_obj$`'s final `ret` pops a
**corrupted return address off the worker's user-heap stack**, jumps into the
user-heap gap, NOP-slides up to `__sys_kernel`, the kernel **restarts**, and the
restart re-runs `_dev_init` which clears `_dev_first` (device chain becomes
empty). Find and fix the stack imbalance so the FDD path completes
(`loader_reached`, depth 10/10).

## Current state (already done — do NOT redo)
- **Derail #1 is FIXED**: `fat_xfer_block$` (`partos/src/os/fat.s` ~770) had
  reverted to a buggy `pop af; or a; call z,fat_dev_read$; call nz,fat_dev_write$`.
  `call z,read` clobbers flags so `call nz,write` fires spuriously with `hl=0`
  (NULL dev). Re-fixed by branching explicitly on the dir flag (`jr nz,fxb_do_write$`).
  This took FDD depth 5→7 (mount now completes, `fat_open` reached). Leave it.
- A **derail catcher** is instrumented in `src/probe_kernel.cpp` (PC ring buffer;
  on the first PC in the user-heap gap `0x0100..base` or re-entry to
  `__sys_kernel` after the block read, it dumps regs + the last ~96 distinct PCs,
  then breaks). KEEP IT — it is the tool that localizes these derails.
  NOTE: it makes `partos_kernel_boot` currently "pass" only because it breaks the
  run *before* the post-derail restart (device chain still intact → boot-floor
  PARTIAL pass). That is NOT a real fix.

## How to reproduce / observe
```
cd /home/tstih/data/iskra-delta/idp-emu
make -C partos kernel              # builds kernel.bin (16384 B, base 0xBF5E)
cmake --build build --target idp-kernel-probe
./bin/idp-kernel-probe             # prints "*** DERAIL after block read ..." + trail
```
Symbols shift every build — ALWAYS resolve addresses from `partos/build/kernel.map`
(the probe's `symbol_map` truncates names to 9 chars; data syms are absolute,
code syms move with `_CODE` base). `partos_full_boot` (HDD) is the authoritative
"kernel works" gate and must stay green.

## Derail #2 — what was caught
```
*** DERAIL after block read: pc=86E2 (user-heap gap)
    af=FEA8 bc=FB43 de=72FF hl=DF1C ix=FB86 iy=C08E sp=0202
    PC trail (newest end): ... C014..C028 (_evt_set body) DF1C DF1D DF1E -> 86E2
```
Decoded against the map (base 0xBF5E):
- Trail = `_evt_set` (C003) running its internal `_list_find`/`_list_match_eq`
  (C08x–C0Cx) with `_ir_disable`/`_ir_enable` (C031/C039), returning into
  `fat_complete_obj$` (~DF04), whose `ret` (~DF1B) pops **86E2** (garbage).
- `sp=0202` = a **thread's user-heap stack** (NOT the dedicated ISR stack at
  0xFD80, and NOT `_SYSVARS`). So the corrupted slot is the return pushed when
  `fat_handle_lookup$` was `call`ed by the worker dispatch (`fw_lookup$` in
  `fat.s`, which expects to return to `fw_after$`). Something between
  `call fat_handle_lookup$` and the final completion `ret` imbalanced/overwrote
  the worker stack.

## Ruled out
- `_evt_set` (`src/kernel/evt.s` ~80) and its 1-byte-stacked-newstate contract:
  verified balanced (`pop hl; inc sp; jp (hl)`), and `fat_complete_obj$`
  (`fat.s` ~246: `push de; push af; inc sp; call _evt_set; pop de; ret`) matches it.
- ISR-on-worker-stack: `drv_isr_enter`/`drv_isr_exit` (`src/drivers/drv.s` ~59)
  DO switch to the dedicated system ISR stack (`DRV_ISR_STACK_TOP`=0xFD80) and
  restore it; the ISR cannot overflow the worker stack.
- DMA/buffer overrun past `fat_sector$` (F8DC, 256 B): its neighbors are FAT scan
  vars in `_SYSVARS`, not the user-heap worker stack — wrong memory region.
- The new `fat_readdir` code: never runs at boot (no caller); its scanner edits
  are byte-for-byte behavior-identical for walk modes 0/1 (lookup/create).

## Prime next suspect
The `_list_find` caller-clean contract used inside `_evt_set`: `_evt_set` does
3 pushes (prev scratch, arg=event, match=`_list_match_eq`), `call _list_find`,
then 3 pops. If `_list_find` (or the `_list_match_eq` callback, sdcccall1) cleans
or leaves the wrong number of stacked bytes, `_evt_set` pops the wrong slots and
ultimately corrupts the caller's return. Audit `_list_find`/`_list_match_eq`
(`src/kernel/list.s`) stack discipline against how `_evt_set` calls them.
Also worth checking: `fat_handle_lookup$` (`src/os/fat_lookup.s`) FILE_ONLY/open
path push/pop balance from `call fat_handle_lookup$` through `fhl_finish$`.

## Why it's FDD-only / why it surfaced now
HDD (`full_boot`) drivers happen to return register/flag/stack states that dodge
both derails; FDD does not. Derail #2 is an old, address-sensitive corruption
(documented across many sessions in the agent memory
`project_kernel_test_harness.md`). It re-surfaced when a 135-byte `_CODE` shift
(adding `fat_readdir`) moved layout — but the bug is pre-existing, not caused by
readdir. Read that memory file for the full multi-session history (re-entrant
VBL fix, ISR-stack fix, the lost-and-restored `fat_xfer_block` flags fix, etc.).

## Definition of done
`./bin/idp-kernel-probe` reaches `loader_reached` (no DERAIL printed),
`partos_kernel_boot` PASSES without relying on the catcher's early break, and
`partos_full_boot` stays green. Then the catcher can be demoted to fail-on-derail.

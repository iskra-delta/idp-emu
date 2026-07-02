;--------------------------------------------------------
; File Created by SDCC : free open source ISO C Compiler
; Version 4.5.20 #16281 (Linux)
;--------------------------------------------------------
	.module cp
	
	.optsdcc -mz80 sdcccall(1)
;--------------------------------------------------------
; Public variables in this module
;--------------------------------------------------------
	.globl _main
	.globl _puts
	.globl _app_resolve_path
	.globl _app_wait_status
	.globl _app_write_file
	.globl _app_read_file
	.globl _app_create_file
	.globl _app_open_file
	.globl _app_read_u16
	.globl _app_read_u8
	.globl _app_write_hex16
	.globl _app_write_newline
	.globl _app_write_cstr
	.globl _app_exit_process
	.globl _app_close_event
	.globl _app_open_event
	.globl _app_boot_filesystem
	.globl _app_dead
;--------------------------------------------------------
; special function registers
;--------------------------------------------------------
;--------------------------------------------------------
; ram data
;--------------------------------------------------------
	.area _DATA
_cp_src_path:
	.ds 64
_cp_dst_path:
	.ds 64
_cp_src_file:
	.ds 18
_cp_dst_file:
	.ds 18
_cp_buf:
	.ds 256
_cp_secs:
	.ds 2
;--------------------------------------------------------
; ram data
;--------------------------------------------------------
	.area _INITIALIZED
;--------------------------------------------------------
; absolute ram data
;--------------------------------------------------------
	.area _DABS (ABS)
	.area _DABS (ABS)
;--------------------------------------------------------
; global & static initialisations
;--------------------------------------------------------
	.area _HOME
	.area _GSINIT
	.area _GSFINAL
	.area _GSINIT
;--------------------------------------------------------
; Home
;--------------------------------------------------------
	.area _HOME
	.area _HOME
;--------------------------------------------------------
; code
;--------------------------------------------------------
	.area _CODE
;../lib/app.h:66: static uint8_t app_file_is_256_aligned(const fat_file_t *file)
;	---------------------------------
; Function app_file_is_256_aligned
; ---------------------------------
_app_file_is_256_aligned:
;../lib/app.h:68: return (((uint8_t)(file->size & 0x00ffu)) == 0u) &&
	inc	hl
	inc	hl
	ld	c, (hl)
	inc	hl
	inc	hl
	inc	hl
	ld	d, (hl)
	ld	a, c
;../lib/app.h:69: (((uint8_t)(file->size >> 24)) == 0u);
	or	a, a
	jr	nz, 00103$
	or	a, d
	jr	z, 00104$
00103$:
	xor	a, a
	ret
00104$:
	ld	a, #0x01
;../lib/app.h:70: }
	ret
;../lib/app.h:72: static uint16_t app_file_sector_count(const fat_file_t *file)
;	---------------------------------
; Function app_file_sector_count
; ---------------------------------
_app_file_sector_count:
;../lib/app.h:74: return (uint16_t)(file->size >> 8);
	inc	hl
	inc	hl
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
;../lib/app.h:75: }
	ret
;cp.c:16: static void cp_exit_now(const char *text)
;	---------------------------------
; Function cp_exit_now
; ---------------------------------
_cp_exit_now:
;cp.c:18: app_close_event();
	push	hl
	call	_app_close_event
	pop	hl
;cp.c:19: if (text != 0) {
	ld	a, h
	or	a, l
	jr	z, 00102$
;cp.c:20: puts(text);
	call	_puts
00102$:
;cp.c:22: app_exit_process();
	call	_app_exit_process
;cp.c:23: app_dead();
;cp.c:24: }
	jp	_app_dead
_cp_usage_text:
	.ascii "usage: cp SRC DST"
	.db 0x00
_cp_align_text:
	.ascii "only 256-byte aligned files"
	.db 0x00
_cp_error_text:
	.ascii "?"
	.db 0x00
_cp_mounted_text:
	.ascii " m="
	.db 0x00
_cp_status_text:
	.ascii " s="
	.db 0x00
_cp_rc_text:
	.ascii " rc="
	.db 0x00
;cp.c:26: int main(int argc, char **argv)
;	---------------------------------
; Function main
; ---------------------------------
_main::
	push	ix
	ld	ix,	#0
	add	ix, sp
	push	af
	push	af
;cp.c:31: fs = app_boot_filesystem();
	push	hl
	push	de
	call	_app_boot_filesystem
	ld	-4 (ix), e
	ld	-3 (ix), d
	pop	de
	pop	hl
;cp.c:32: if ((fs == 0) || (app_open_event() == 0)) {
	ld	a, -3 (ix)
	or	a, -4 (ix)
	jr	z, 00101$
	push	hl
	push	de
	call	_app_open_event
	ld	c, e
	ld	b, d
	pop	de
	pop	hl
	ld	a, b
	or	a, c
	jr	nz, 00102$
00101$:
;cp.c:33: cp_exit_now(cp_error_text);
	ld	hl, #_cp_error_text
	call	_cp_exit_now
;cp.c:34: return 1;
	ld	de, #0x0001
	jp	00132$
00102$:
;cp.c:36: if ((argc != 3) ||
	ld	a, l
	sub	a, #0x03
	or	a, h
	jp	nz, 00129$
;cp.c:37: !app_resolve_path(cp_src_path, APP_PATH_CAP, argv[1]) ||
	ld	l, e
	ld	h, d
	inc	hl
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	push	de
	push	bc
	ld	a, #0x40
	push	af
	inc	sp
	ld	hl, #_cp_src_path
	call	_app_resolve_path
	pop	de
	or	a, a
	jp	z, 00129$
;cp.c:38: !app_resolve_path(cp_dst_path, APP_PATH_CAP, argv[2])) {
	ld	hl, #4
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	push	bc
	ld	a, #0x40
	push	af
	inc	sp
	ld	hl, #_cp_dst_path
	call	_app_resolve_path
	or	a, a
	jp	z, 00129$
;cp.c:42: if (app_open_file(fs, cp_src_path, &cp_src_file) != FAT_OK) {
	ld	hl, #_cp_src_file
	push	hl
	ld	de, #_cp_src_path
	ld	l, -4 (ix)
	ld	h, -3 (ix)
	call	_app_open_file
	ld	a, d
	or	a, e
	jp	nz, 00131$
;cp.c:45: if (app_wait_status(&cp_src_file.status) != FAT_OK) {
	ld	hl, #(_cp_src_file + 10)
	call	_app_wait_status
	ld	a, d
	or	a, e
	jp	nz, 00131$
;cp.c:48: if (!app_file_is_256_aligned(&cp_src_file)) {
	ld	hl, #_cp_src_file
	call	_app_file_is_256_aligned
	or	a, a
	jp	z, 00130$
;cp.c:51: cp_secs = app_file_sector_count(&cp_src_file);
	ld	hl, #_cp_src_file
	call	_app_file_sector_count
	ld	(_cp_secs), de
;cp.c:53: rc = app_create_file(fs, cp_dst_path, &cp_dst_file);
	ld	hl, #_cp_dst_file
	push	hl
	ld	de, #_cp_dst_path
	ld	l, -4 (ix)
	ld	h, -3 (ix)
	call	_app_create_file
	ld	-2 (ix), e
	ld	-1 (ix), d
;cp.c:54: if (rc != FAT_OK) {
	ld	a, d
	or	a, e
	jr	z, 00115$
;cp.c:55: app_close_event();
	call	_app_close_event
;cp.c:56: puts(cp_error_text);
	ld	hl, #_cp_error_text
	call	_puts
;cp.c:57: app_write_cstr(cp_mounted_text);
	ld	hl, #_cp_mounted_text
	call	_app_write_cstr
;cp.c:58: app_write_hex16((uint16_t)app_read_u8(fs, 27u));
	pop	hl
	push	hl
	push	hl
	ld	a, #0x1b
	push	af
	inc	sp
	call	_app_read_u8
	ld	c, #0x00
	ld	l, a
	ld	h, c
	call	_app_write_hex16
;cp.c:59: app_write_cstr(cp_status_text);
	ld	hl, #_cp_status_text
	call	_app_write_cstr
	pop	hl
;cp.c:60: app_write_hex16(app_read_u16(fs, 28u));
	ld	a, #0x1c
	push	af
	inc	sp
	call	_app_read_u16
	ex	de, hl
	call	_app_write_hex16
;cp.c:61: app_write_cstr(cp_rc_text);
	ld	hl, #_cp_rc_text
	call	_app_write_cstr
;cp.c:62: app_write_hex16((uint16_t)rc);
	pop	de
	pop	hl
	push	hl
	push	de
	call	_app_write_hex16
;cp.c:63: app_write_newline();
	call	_app_write_newline
;cp.c:64: app_exit_process();
	call	_app_exit_process
;cp.c:65: app_dead();
	call	_app_dead
;cp.c:66: return 1;
	ld	de, #0x0001
	jr	00132$
00115$:
;cp.c:68: if (app_wait_status(&cp_dst_file.status) != FAT_OK) {
	ld	hl, #(_cp_dst_file + 10)
	call	_app_wait_status
	ld	a, d
	or	a, e
	jr	nz, 00131$
;cp.c:72: while (cp_secs != 0u) {
00126$:
	ld	a, (_cp_secs+1)
	ld	hl, #_cp_secs
	or	a, (hl)
	jr	z, 00128$
;cp.c:73: if (app_read_file(&cp_src_file, cp_buf, 256u) != FAT_OK) {
	ld	hl, #0x0100
	push	hl
	ld	de, #_cp_buf
	ld	hl, #_cp_src_file
	call	_app_read_file
	ld	a, d
	or	a, e
	jr	nz, 00131$
;cp.c:76: if (app_wait_status(&cp_src_file.status) != FAT_OK) {
	ld	hl, #(_cp_src_file + 10)
	call	_app_wait_status
	ld	a, d
	or	a, e
	jr	nz, 00131$
;cp.c:79: if (app_write_file(&cp_dst_file, cp_buf, 256u) != FAT_OK) {
	ld	hl, #0x0100
	push	hl
	ld	de, #_cp_buf
	ld	hl, #_cp_dst_file
	call	_app_write_file
	ld	a, d
	or	a, e
	jr	nz, 00131$
;cp.c:82: if (app_wait_status(&cp_dst_file.status) != FAT_OK) {
	ld	hl, #(_cp_dst_file + 10)
	call	_app_wait_status
	ld	a, d
	or	a, e
	jr	nz, 00131$
;cp.c:85: cp_secs--;
	ld	hl, (_cp_secs)
	dec	hl
	ld	(_cp_secs), hl
	jr	00126$
00128$:
;cp.c:87: cp_exit_now(0);
	ld	hl, #0x0000
	call	_cp_exit_now
;cp.c:88: return 0;
	ld	de, #0x0000
	jr	00132$
;cp.c:90: cp_usage:
00129$:
;cp.c:91: cp_exit_now(cp_usage_text);
	ld	hl, #_cp_usage_text
	call	_cp_exit_now
;cp.c:92: return 1;
	ld	de, #0x0001
	jr	00132$
;cp.c:94: cp_align:
00130$:
;cp.c:95: cp_exit_now(cp_align_text);
	ld	hl, #_cp_align_text
	call	_cp_exit_now
;cp.c:96: return 1;
	ld	de, #0x0001
	jr	00132$
;cp.c:98: cp_error:
00131$:
;cp.c:99: cp_exit_now(cp_error_text);
	ld	hl, #_cp_error_text
	call	_cp_exit_now
;cp.c:100: return 1;
	ld	de, #0x0001
00132$:
;cp.c:101: }
	ld	sp, ix
	pop	ix
	ret
	.area _CODE
	.area _INITIALIZER
	.area _CABS (ABS)

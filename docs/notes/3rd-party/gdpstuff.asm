	; Razne rutine za GDP (nepreizkusxene)
	; Matej Horvat
	; 22., 23. marec 2021

	; ----------------------------------------

GDPOutStr:
	; Hitro izstavljanje niza ukazov

	; HL -> zaporedje ukazov za GDP
	; B = sxtevilo ukazov
	ld c, 20h	; GDP command/status port

.wait:
	in a, (2Fh)	; GDP status port
	and 4		; Ready flag
	jr z, .wait

	outi
	jr nz, GDPOutStr

	ret

	; ----------------------------------------

GDPRead8:
	; Branje 8 vodoravno zaporednih pikslov

	; (predpostavljam, da sta GDP X in Y registra zxe nastavljena,
	; in da je bil ukaz "pen up" zxe izstavljen)

	ld c, 0		; Sem bodo sxli prebrani piksli
	ld b, 8

.wait1:
	in a, (2Fh)
	and 4		; Ready flag
	jr z, .wait1

	ld a, 0Fh	; Ukaz za branje piksla
	out (20h), a

.wait2:
	in a, (2Fh)
	and 4
	jr z, .wait2

	in a, (36h)
	rla		; CF := piksel (0, cxe je nastavljen!)
	rl c

	; (ugibam, da cxakanje na ready flag tu ni potrebno)

	ld a, 0A0h	; "Narisxi cxrto", delta X = 1, delta Y = 0
	out (20h), a

	djnz .wait1

	ret

	; ----------------------------------------

GDPWrite8:
	; Zapisovanje 8 vodoravno zaporednih pikslov

	; (predpostavljam, da C vsebuje [prej prebrane] piksle,
	; da sta GDP X in Y registra zxe nastavljena, in da HL
	; vsebuje isto vrednost kot GDP X register)

	ld b, 8

.loop:
	xor a
	rl c
	rla		; A := "eraser", cxe piksel ni nastavljen, drugacxe "pen"

.wait1:
	in d, (2Fh)
	bit 2, d	; Ready flag
	jr z, .wait1

	out (20h), a

.wait2:
	in a, (2Fh)
	and 4
	jr z, .wait2

	ld a, 080h	; Narisxi/pobrisxi cxrto, delta X = 0, delta Y = 0
	out (20h), a

	inc hl

.wait3:
	in a, (2Fh)
	and 4
	jr z, .wait3

	out (28h), h
	out (29h), l

	djnz .loop

	ret
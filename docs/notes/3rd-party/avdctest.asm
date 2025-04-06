	org 7000h

Char	db 0		; ORG + 0
Attr	db 0		; ORG + 1
	jr Izpisi	; ORG + 2
	dw 0		; ORG + 4
	dw 0		; ORG + 6
;	dw 0		; ORG + 8
;	dw 0		; ORG + 10
;	dw 0		; ORG + 12
;	dw 0		; ORG + 14

	; Rutine odkomentiraj po potrebi…

; ----------------------------------------

;NastaviNaslov:
	; Nastavi naslov kurzorja (Char je spodnji bajt, Attr je zgornji bajt).

;	call Pocakaj

;	ld a, (Char)
;	out (3Ch), a
;	ld a, (Attr)
;	out (3Dh), a

;	ret

; ----------------------------------------

;PreberiNaslov:
	; Prebere naslov kurzorja.

;	call Pocakaj

;	in a, (3Ch)
;	ld (Char), a
;	in a, (3Dh)
;	ld (Attr), a

;	ret

; ----------------------------------------

;PreberiSS1:
	; Prebere screen start 1.

;	call Pocakaj

;	in a, (3Ah)
;	ld (Char), a
;	in a, (3Bh)
;	ld (Attr), a

;	ret

; ----------------------------------------

Pocakaj:
	; Cxaka, da AVDC ni zaseden.

.cakajAccess:
	in a, (36h)
	and 10h		; AVDC access flag
	jr nz, .cakajAccess

.cakajReady:
	in a, (39h)
	and 20h		; Ready flag
	jr z, .cakajReady

	ret

; ----------------------------------------

Izpisi:
	; Izpisxe znak na AVDC na mesto kurzorja in ga povecxa.

	call Pocakaj

	ld a, 30h	; Cursor off
	out (39h), a

	ld a, (Char)
	out (34h), a

	ld a, (Attr)
	out (35h), a

	ld a, 0ABh	; Write at cursor address and increment
	out (39h), a

	ld a, 35h	; Cursor on
	out (39h), a

	ret

; ----------------------------------------

;PreberiZnak:
	; Prebere znak iz AVDC spomina.

	; TODO: testiraj sxe enkrat

;	call Pocakaj

;	ld a, 0ADh	; Read at cursor and increment
;	out (39h), a

;	call Pocakaj

;	in a, (34h)
;	ld (Char), a
;	in a, (35h)
;	ld (Attr), a

;	ret

; ----------------------------------------

;OmogociDouble:
;	call Pocakaj

;	ld a, 1Eh	; Load IR pointer with value (14)
;	out (39h), a

;	ld a, 0F0h
;	out (38h), a

;	ret

; ----------------------------------------

;IzmeriCas:
	; Izmeri cxas, potreben za dokoncxanje delayed ukaza.

;	call Pocakaj

;	ld a, (Char)
;	out (39h), a

;	ld bc, $FFFF
;	ld e, 20h	; Ready flag
;.cakajReady:
;	inc bc
;	in a, (39h)
;	and e		; (hitreje kot immediate verzija)
;	jr z, .cakajReady

;	ld (Char), bc

;	ret

; ----------------------------------------

;NastaviIR:
	; Nastavi poljuben IR s poljubno vrednostjo.
	; TODO: eksperimentiraj s tem pozneje - ocxitno ima
	; potencial, da sesuje cel racxunalnik

;	call Pocakaj

;	ld a, (Char)	; Ukaz - mora biti 1Xh
;	out (39h), a

;	call Pocakaj

;	ld a, (Attr)
;	out (38h), a

;	ret

; ----------------------------------------

;ZajemajA0:
	; Zajema podatke s porta A0.
	; To sicer ni del AVDC, a vseeno.

;	ld hl, .buffer
;	ld b, 0		; 256 ponovitev

;.naslednja:
;	in a, (0A0h)
;	ld (hl), a
;	inc hl
;	djnz .naslednja

;	ret

;.buffer:
	; Sem grejo prebrani podatki
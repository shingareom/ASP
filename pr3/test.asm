; ============================================================
;  test.asm  – Hypothetical Machine test program
;  Computes: RESULT = (A + B) * C - D
;  Demonstrates: labels, forward refs, all directives,
;                all opcodes.
; ============================================================

        ORG  0x000       ; start at address 0

; ----- load A -----
START:  LOAD  A           ; AC ← A          (forward ref to A)
        ADD   B           ; AC ← AC + B      (forward ref to B)
        STOR  TEMP        ; TEMP ← AC+B

; ----- multiply by C (repeated addition) -----
        LOAD  ZERO        ; AC ← 0
        STOR  RESULT      ; RESULT ← 0
        LOAD  C           ; AC ← C  (loop counter)
        STOR  CNT         ; CNT ← C

LOOP:   JZERO DONE        ; if CNT==0 goto DONE
        LOAD  RESULT
        ADD   TEMP        ; RESULT ← RESULT + TEMP
        STOR  RESULT
        LOAD  CNT
        SUB   ONE         ; CNT--
        STOR  CNT
        JUMP  LOOP

; ----- subtract D -----
DONE:   LOAD  RESULT
        SUB   D           ; RESULT ← RESULT - D
        STOR  RESULT

; ----- check sign -----
        JNEG  NEGPATH     ; if RESULT < 0 jump
        JZERO ZEROPATH    ; if RESULT == 0 jump
        JUMP  FINISH      ; positive path – fall through

NEGPATH:  NOP             ; placeholder for negative handling
          JUMP FINISH

ZEROPATH: NOP             ; placeholder for zero handling
          JUMP FINISH

FINISH:   HALT            ; stop

; ============================================================
;  Data section (ORG to a new area)
; ============================================================
        ORG  0x100

A:      DC   5            ; A  =  5
B:      DC   3            ; B  =  3
C:      DC   4            ; C  =  4  (multiply factor)
D:      DC   6            ; D  =  6
ONE:    DC   1
ZERO:   DC   0
TEMP:   DS   1            ; 1 word of scratch storage
RESULT: DS   1
CNT:    DS   1

        END

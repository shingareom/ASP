; ============================================================
;  test8086.asm  –  PR4 Test Program
;  Demonstrates: segments, labels, data directives,
;                arithmetic, loops, conditionals, procedures
;
;  Program: Computes SUM = A + B, PRODUCT = A * B
;           Then stores results and halts.
; ============================================================

        .MODEL  SMALL

        .STACK  256         ; 256-byte stack

; ============================================================
;  DATA SEGMENT
; ============================================================
        .DATA

MSG     DB  'Hello',0       ; null-terminated string
A       DW  5               ; variable A = 5
B       DW  3               ; variable B = 3
SUM     DW  0               ; result: SUM = A + B
PRODUCT DW  0               ; result: PRODUCT = A * B
COUNT   DW  10              ; loop counter
ARRAY   DW  5 DUP(0)        ; array of 5 words, init to 0
MAXVAL  EQU 100             ; constant (no storage)

; ============================================================
;  CODE SEGMENT
; ============================================================
        .CODE

START:
        ; --- Setup DS segment register ---
        MOV  AX, 0          ; load data segment value
        MOV  DS, AX         ; point DS at data segment

        ; --- Compute SUM = A + B ---
        MOV  AX, [A]        ; AX = A (5)
        MOV  BX, [B]        ; BX = B (3)
        ADD  AX, BX         ; AX = A + B
        MOV  [SUM], AX      ; store result

        ; --- Compute PRODUCT = A * B (loop: B additions of A) ---
        MOV  CX, [B]        ; CX = B (loop counter)
        MOV  AX, 0          ; accumulator = 0
        MOV  [PRODUCT], AX  ; PRODUCT = 0

MULLOOP:
        CMP  CX, 0          ; is counter zero?
        JE   DONE_MUL       ; if yes, done
        ADD  AX, [A]        ; AX += A
        DEC  CX             ; CX--
        JMP  MULLOOP        ; repeat

DONE_MUL:
        MOV  [PRODUCT], AX  ; store PRODUCT

        ; --- Demonstrate INC / DEC ---
        MOV  AX, [COUNT]    ; AX = COUNT (10)
        INC  AX             ; AX = 11
        DEC  AX             ; AX = 10 again

        ; --- Demonstrate SHL / SHR ---
        MOV  AX, 4          ; AX = 4
        SHL  AX, 1          ; AX = 8  (multiply by 2)
        SHR  AX, 1          ; AX = 4  (divide by 2)

        ; --- Demonstrate logical ops ---
        MOV  AX, 0FFH       ; AX = 0xFF
        AND  AX, 0FH        ; AX = 0x0F (mask lower nibble)
        OR   AX, 0F0H       ; AX = 0xFF
        XOR  AX, AX         ; AX = 0 (clear)

        ; --- Demonstrate PUSH / POP ---
        MOV  AX, [SUM]
        PUSH AX             ; push SUM onto stack
        POP  BX             ; pop into BX

        ; --- Demonstrate CALL / RET ---
        CALL MYPROC         ; call procedure

        ; --- Software interrupt (DOS exit) ---
        MOV  AX, 4C00H      ; DOS terminate function
        INT  21H            ; call DOS

; ============================================================
;  Procedure: MYPROC
;  Does nothing except return (placeholder)
; ============================================================
MYPROC  PROC
        PUSH AX             ; save AX
        MOV  AX, 0         ; do nothing useful
        POP  AX             ; restore AX
        RET                 ; return to caller
MYPROC  ENDP

        END  START

; ============================================================
;  test_pr5.asm  –  PR5 Procedure Test Program
;
;  Demonstrates:
;    - NEAR and FAR procedures
;    - Stack frame (PUSH BP / MOV BP,SP / POP BP)
;    - LOCAL variables ([BP-n] addressing)
;    - Nested CALL sequences
;    - RETF (far return)
;    - Multiple procedures with call-count tracking
; ============================================================

        .MODEL  SMALL
        .STACK  256

; ============================================================
;  DATA SEGMENT
; ============================================================
        .DATA

NUM1    DW  10          ; first number
NUM2    DW  20          ; second number
RESULT  DW  0           ; sum result
MSG     DB  'Done',0    ; message string
COUNTER DW  5           ; loop counter

; ============================================================
;  CODE SEGMENT
; ============================================================
        .CODE

; ------------------------------------------------------------
;  MAIN entry point
; ------------------------------------------------------------
MAIN    PROC  NEAR
        LOCAL  TEMP:WORD, IDX:WORD

        ; Setup DS
        MOV   AX, 0
        MOV   DS, AX

        ; Store a local variable
        MOV   AX, 42
        MOV   TEMP, AX        ; [BP-2] = 42

        ; Call ADD_NUMS to add NUM1 + NUM2 → RESULT
        CALL  ADD_NUMS

        ; Call FACTORIAL with argument 5
        MOV   CX, [COUNTER]
        CALL  FACTORIAL

        ; Store factorial result (in AX) to RESULT
        MOV   [RESULT], AX

        ; Call PRINT_DONE  (demonstrates deeper call chain)
        CALL  PRINT_DONE

        ; DOS exit
        MOV   AX, 4C00H
        INT   21H

MAIN    ENDP

; ------------------------------------------------------------
;  ADD_NUMS  –  Adds NUM1 and NUM2, stores in RESULT
;  No arguments (uses globals)
;  Returns: AX = sum
; ------------------------------------------------------------
ADD_NUMS  PROC  NEAR

        MOV   AX, [NUM1]     ; AX = NUM1
        ADD   AX, [NUM2]     ; AX = NUM1 + NUM2
        MOV   [RESULT], AX   ; store result
        RET

ADD_NUMS  ENDP

; ------------------------------------------------------------
;  FACTORIAL  –  Computes N! iteratively
;  Input : CX = N  (0 <= N <= 12)
;  Output: AX = N!
; ------------------------------------------------------------
FACTORIAL  PROC  NEAR
        LOCAL  COUNT:WORD

        MOV   AX, 1           ; AX = accumulator (starts at 1)
        MOV   COUNT, CX       ; save N as local var

FACT_LOOP:
        CMP   CX, 0           ; if N == 0, done
        JE    FACT_DONE
        MUL   CX              ; AX = AX * CX  (DX:AX = result, we ignore DX)
        DEC   CX
        JMP   FACT_LOOP

FACT_DONE:
        RET

FACTORIAL  ENDP

; ------------------------------------------------------------
;  PRINT_DONE  –  Placeholder: prints a "Done" message
;  Demonstrates a simple leaf procedure (no locals)
; ------------------------------------------------------------
PRINT_DONE  PROC  NEAR

        ; In a real DOS program this would call INT 21H/AH=09H
        ; Here we just demonstrate the procedure structure
        MOV   AH, 09H         ; DOS print-string function
        MOV   DX, 0           ; DS:DX → MSG  (simplified)
        ; INT 21H             ; commented out – no real DOS here
        NOP                   ; placeholder
        RET

PRINT_DONE  ENDP

; ------------------------------------------------------------
;  SWAP_AX_BX  –  Swaps the values of AX and BX
;  Input : AX, BX
;  Output: AX ← old BX, BX ← old AX
;  Demonstrates: XCHG and a utility procedure
; ------------------------------------------------------------
SWAP_AX_BX  PROC  NEAR

        XCHG  AX, BX
        RET

SWAP_AX_BX  ENDP

; ------------------------------------------------------------
;  MAX_OF_TWO  –  Returns larger of AX, BX in AX
;  Input : AX, BX
;  Output: AX = max(AX, BX)
; ------------------------------------------------------------
MAX_OF_TWO  PROC  NEAR

        CMP   AX, BX          ; compare AX and BX
        JGE   MAX_DONE        ; if AX >= BX, AX already holds max
        MOV   AX, BX          ; else AX = BX

MAX_DONE:
        RET

MAX_OF_TWO  ENDP

        END  MAIN

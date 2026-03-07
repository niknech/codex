; ------------------------------------------------------------
; hex16_to_dec.asm
; Целевая CPU: Z80
; Мнемоника:   Intel 8080 (без Z80-специфичных инструкций)
;
; Подпрограмма START:
;   Вход : HL = 16-битное число (0..65535)
;   Выход: DEC_BUFFER = десятичная строка, 0-терминированная
; ------------------------------------------------------------

        ORG     0000H

START:
        ; Сохранить входное значение, т.к. HL используется как указатель
        SHLD    WORK_VALUE

        ; Инициализация выходного буфера
        LXI     H, DEC_BUFFER
        SHLD    OUT_PTR

        ; Пока не выведена первая ненулевая цифра
        XRA     A
        STA     STARTED

        ; Восстановить текущее значение в HL
        LHLD    WORK_VALUE

        ; Разряды: 10000, 1000, 100, 10
        LXI     D, 2710H         ; 10000
        CALL    EMIT_OPTIONAL_DIGIT

        LXI     D, 03E8H         ; 1000
        CALL    EMIT_OPTIONAL_DIGIT

        LXI     D, 0064H         ; 100
        CALL    EMIT_OPTIONAL_DIGIT

        LXI     D, 000AH         ; 10
        CALL    EMIT_OPTIONAL_DIGIT

        ; Последний разряд (1) печатаем всегда
        LXI     D, 0001H
        CALL    MAKE_DIGIT
        CALL    STORE_CHAR

        ; 0-терминатор строки (для puts)
        XRA     A
        CALL    STORE_CHAR

        RET

; ------------------------------------------------------------
; EMIT_OPTIONAL_DIGIT
; Вход:
;   HL = текущее значение
;   DE = делитель (10000/1000/100/10)
; Выход:
;   HL = остаток
; ------------------------------------------------------------
EMIT_OPTIONAL_DIGIT:
        CALL    MAKE_DIGIT        ; A='0'..'9', HL=remainder

        ; Сохранить остаток HL на время печати
        PUSH    H

        MOV     B, A              ; сохранить цифру
        LDA     STARTED
        ORA     A
        JNZ     EOD_PRINT

        MOV     A, B
        CPI     '0'
        JZ      EOD_SKIP

        ; Первая ненулевая цифра
        MVI     A, 1
        STA     STARTED

EOD_PRINT:
        MOV     A, B
        CALL    STORE_CHAR

EOD_SKIP:
        POP     H
        RET

; ------------------------------------------------------------
; MAKE_DIGIT
; Вход:
;   HL = текущее значение
;   DE = делитель
; Выход:
;   HL = остаток
;   A  = ASCII-цифра
; ------------------------------------------------------------
MAKE_DIGIT:
        MVI     B, 0

MD_LOOP:
        ; Если HL < DE -> закончить
        MOV     A, H
        CMP     D
        JC      MD_DONE
        JNZ     MD_SUB

        MOV     A, L
        CMP     E
        JC      MD_DONE

MD_SUB:
        ; HL = HL - DE
        MOV     A, L
        SUB     E
        MOV     L, A

        MOV     A, H
        SBB     D
        MOV     H, A

        INR     B
        JMP     MD_LOOP

MD_DONE:
        MOV     A, B
        ADI     '0'
        RET

; ------------------------------------------------------------
; STORE_CHAR
; Вход:
;   A = символ для записи
; ------------------------------------------------------------
STORE_CHAR:
        PUSH    H

        LHLD    OUT_PTR
        MOV     M, A
        INX     H
        SHLD    OUT_PTR

        POP     H
        RET

; ------------------------------------------------------------
; Данные
; ------------------------------------------------------------
OUT_PTR:
        DW      0000H

STARTED:
        DB      00H

WORK_VALUE:
        DW      0000H

DEC_BUFFER:
        DB      '00000',00H

        END

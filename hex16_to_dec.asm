; ------------------------------------------------------------
; hex16_to_dec.asm
; Целевая CPU: Z80
; Мнемоника:   Intel 8080 (без Z80-специфичных инструкций)
;
; Назначение:
;   Перевод 16-битного числа из HEX (двоичное значение WORD)
;   в десятичную ASCII-строку и вывод через BDOS function 9.
; ------------------------------------------------------------

        ORG     100H

START:
        ; HL <- исходное 16-битное число
        LHLD    HEX_VALUE

        ; Инициализация выходного буфера
        LXI     H, DEC_BUFFER
        SHLD    OUT_PTR

        ; Пока не выведена первая ненулевая цифра: STARTED = 0
        XRA     A
        STA     STARTED

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

        ; Завершитель строки для BDOS function 9
        MVI     A, '$'
        CALL    STORE_CHAR

        ; Вывод результата
        LXI     D, DEC_BUFFER
        MVI     C, 9
        CALL    5

        RET

; ------------------------------------------------------------
; EMIT_OPTIONAL_DIGIT
; Вход:
;   HL = текущее значение
;   DE = делитель (10000/1000/100/10)
; Логика:
;   - пока STARTED=0, нулевые цифры пропускаются
;   - после первой ненулевой цифры печатаются все разряды
; Выход:
;   HL = остаток
; ------------------------------------------------------------
EMIT_OPTIONAL_DIGIT:
        CALL    MAKE_DIGIT        ; A = '0'..'9', HL = remainder

        PUSH    H
        PUSH    D

        MOV     B, A              ; сохранить цифру
        LDA     STARTED
        ORA     A
        JNZ     EOD_PRINT

        MOV     A, B
        CPI     '0'
        JZ      EOD_SKIP

        ; первая ненулевая цифра
        MVI     A, 1
        STA     STARTED

EOD_PRINT:
        MOV     A, B
        CALL    STORE_CHAR

EOD_SKIP:
        POP     D
        POP     H
        RET

; ------------------------------------------------------------
; MAKE_DIGIT
; Вход:
;   HL = текущее значение
;   DE = десятичный делитель
; Выход:
;   HL = остаток после вычитаний
;   A  = ASCII-цифра ('0'..'9')
; ------------------------------------------------------------
MAKE_DIGIT:
        MVI     B, 0

MD_LOOP:
        ; Если HL < DE, завершаем
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
; Использует OUT_PTR как текущий указатель буфера.
; ------------------------------------------------------------
STORE_CHAR:
        PUSH    B
        PUSH    H

        LHLD    OUT_PTR
        MOV     M, A
        INX     H
        SHLD    OUT_PTR

        POP     H
        POP     B
        RET

; ------------------------------------------------------------
; Данные
; ------------------------------------------------------------
HEX_VALUE:
        DW      0ABCDH            ; пример: 0xABCD = 43981

OUT_PTR:
        DW      0000H

STARTED:
        DB      00H

DEC_BUFFER:
        DB      '00000$'          ; буфер максимум 5 цифр + '$'

        END     START

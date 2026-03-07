; ------------------------------------------------------------
; hex16_to_dec.asm
; Intel 8080 / CP/M
; Перевод 16-битного числа из шестнадцатеричной формы в
; десятичную строку (5 символов) и вывод через BDOS function 9.
; ------------------------------------------------------------

        ORG     100h

START:
        ; HL <- исходное 16-битное число
        LHLD    HEX_VALUE

        ; Инициализация указателя на выходной буфер
        LXI     H, DEC_BUFFER
        SHLD    OUT_PTR

        ; 10000
        LXI     D, 2710h
        CALL    MAKE_DIGIT
        CALL    STORE_CHAR

        ; 1000
        LXI     D, 03E8h
        CALL    MAKE_DIGIT
        CALL    STORE_CHAR

        ; 100
        LXI     D, 0064h
        CALL    MAKE_DIGIT
        CALL    STORE_CHAR

        ; 10
        LXI     D, 000Ah
        CALL    MAKE_DIGIT
        CALL    STORE_CHAR

        ; 1
        LXI     D, 0001h
        CALL    MAKE_DIGIT
        CALL    STORE_CHAR

        ; Терминатор для BDOS function 9
        MVI     A, '$'
        CALL    STORE_CHAR

        ; Вывод результата
        LXI     D, DEC_BUFFER
        MVI     C, 9
        CALL    5

        RET

; ------------------------------------------------------------
; MAKE_DIGIT
; Вход:
;   HL = текущее значение
;   DE = десятичный делитель (10000,1000,100,10,1)
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
        DW      0ABCDh          ; Пример: AB CDh = 43981d

OUT_PTR:
        DW      0000h

DEC_BUFFER:
        DB      '00000$'

        END     START

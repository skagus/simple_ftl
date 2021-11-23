
.code 

;---- ---- ---- ---- ---- ---- ---- ----
; coroutine yield function
;
;   : void yield_( void * token );
;
;   'token' -&amp;gt; RCX
;
yield_ proc

    push RBX
    push RBP
    push RDI
    push RSI
    push R12
    push R13
    push R14
    push R15

    mov  RAX ,  RSP
    mov  RSP , [RCX]
    mov [RCX],  RAX

    pop R15
    pop R14
    pop R13
    pop R12
    pop RSI
    pop RDI
    pop RBP
    pop RBX

    ret

yield_ endp

;---- ---- ---- ---- ---- ---- ---- ----
; enter a co-routine
;
;   : void enter_( void * token, void * arg1, ... );
;
;   'token'     -&amp;gt; RCX
;   'arg1, ...' -&amp;gt; RDX, R8, and R9
;
enter_ proc

    jmp yield_

enter_ endp

end

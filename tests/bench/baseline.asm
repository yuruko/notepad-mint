; 1.0.3 newline counter, retained only as a reproducible benchmark baseline.
.686
.xmm
.model flat
option casemap:none
public _mp_count_lf_before
.code
_mp_count_lf_before proc
    push    esi
    push    edi
    mov     ecx, [esp + 12]         ; p
    mov     edx, [esp + 16]         ; n
    mov     eax, 0Ah
    movd    xmm0, eax
    pshuflw xmm0, xmm0, 0           ; 0x000a in the low 4 words
    pshufd  xmm0, xmm0, 0           ; ... and in all 8
    xor     eax, eax                ; running count (counts mask bits: 2 per match, until cl_tail)
    cmp     edx, 8
    jb      cl_tail
cl_vec:
    movdqu  xmm1, xmmword ptr [ecx]
    pcmpeqw xmm1, xmm0              ; 0xffff where the word is 0x000a
    pmovmskb esi, xmm1              ; 2 mask bits per word
    test    esi, esi
    jz      cl_next
cl_bits:
    lea     edi, [esi - 1]
    and     esi, edi                ; clear the lowest set bit
    inc     eax
    test    esi, esi
    jnz     cl_bits
cl_next:
    add     ecx, 16
    sub     edx, 8
    cmp     edx, 8
    jae     cl_vec
cl_tail:
    shr     eax, 1                  ; mask bits -> matches
    test    edx, edx
    jz      cl_done
cl_scalar:
    cmp     word ptr [ecx], 0Ah
    jne     cl_next2
    inc     eax
cl_next2:
    add     ecx, 2
    dec     edx
    jnz     cl_scalar
cl_done:
    pop     edi
    pop     esi
    ret
_mp_count_lf_before endp
end

.686
.xmm
.model flat
option casemap:none

public _memset
public _memcpy
public _memmove
public _memcmp
public __chkstk
public _mp_count_lf

.code

_memset proc
    push    edi
    mov     edi, [esp + 8]
    mov     eax, [esp + 12]
    mov     ecx, [esp + 16]
    mov     edx, edi
    rep     stosb
    mov     eax, edx
    pop     edi
    ret
_memset endp

_memcpy proc
    push    esi
    push    edi
    mov     edi, [esp + 12]
    mov     esi, [esp + 16]
    mov     ecx, [esp + 20]
    mov     eax, edi
    mov     edx, ecx
    shr     ecx, 2
    rep     movsd
    mov     ecx, edx
    and     ecx, 3
    rep     movsb
    pop     edi
    pop     esi
    ret
_memcpy endp

_memmove proc
    push    esi
    push    edi
    mov     edi, [esp + 12]
    mov     esi, [esp + 16]
    mov     ecx, [esp + 20]
    mov     eax, edi
    cmp     edi, esi
    jbe     mm_fwd
    lea     edx, [esi + ecx]
    cmp     edi, edx
    jae     mm_fwd
    lea     edi, [edi + ecx - 1]
    lea     esi, [esi + ecx - 1]
    mov     edx, ecx
    and     ecx, 3
    std
    rep     movsb
    sub     esi, 3
    sub     edi, 3
    mov     ecx, edx
    shr     ecx, 2
    rep     movsd
    cld
    jmp     mm_done
mm_fwd:
    mov     edx, ecx
    shr     ecx, 2
    rep     movsd
    mov     ecx, edx
    and     ecx, 3
    rep     movsb
mm_done:
    pop     edi
    pop     esi
    ret
_memmove endp

_memcmp proc
    push    esi
    push    edi
    mov     esi, [esp + 12]
    mov     edi, [esp + 16]
    mov     ecx, [esp + 20]
mc_dw:
    cmp     ecx, 4
    jb      mc_bytes
    mov     eax, [esi]
    cmp     eax, [edi]
    jne     mc_diff
    add     esi, 4
    add     edi, 4
    sub     ecx, 4
    jmp     mc_dw
mc_diff:
    mov     ecx, 4
mc_bytes:
    xor     eax, eax
    test    ecx, ecx
    jz      mc_done
mc_loop:
    movzx   eax, byte ptr [esi]
    movzx   edx, byte ptr [edi]
    sub     eax, edx
    jnz     mc_done
    inc     esi
    inc     edi
    dec     ecx
    jnz     mc_loop
mc_done:
    pop     edi
    pop     esi
    ret
_memcmp endp

__chkstk proc
    push    ecx
    lea     ecx, [esp + 8]
    sub     ecx, eax
    sbb     eax, eax
    not     eax
    and     ecx, eax
    mov     eax, esp
    and     eax, 0FFFFF000h
cs_check:
    cmp     ecx, eax
    jb      cs_probe
    mov     eax, ecx
    pop     ecx
    xchg    esp, eax
    mov     eax, [eax]
    push    eax
    ret
cs_probe:
    sub     eax, 1000h
    test    dword ptr [eax], eax
    jmp     cs_check
__chkstk endp

_mp_count_lf proc
    mov     ecx, [esp + 4]
    mov     edx, [esp + 8]
    xor     eax, eax
    cmp     edx, 8
    jb      cl_tail
    mov     eax, 0Ah
    movd    xmm0, eax
    pshuflw xmm0, xmm0, 0
    pshufd  xmm0, xmm0, 0
    pxor    xmm2, xmm2
    pxor    xmm3, xmm3
cl_vec:
    movdqu  xmm1, xmmword ptr [ecx]
    pcmpeqw xmm1, xmm0
    pmovmskb eax, xmm1
    test    eax, eax
    jz      cl_next
    psrlw   xmm1, 15
    psadbw  xmm1, xmm2
    paddd   xmm3, xmm1
cl_next:
    add     ecx, 16
    sub     edx, 8
    cmp     edx, 8
    jae     cl_vec
    movdqa  xmm1, xmm3
    psrldq  xmm1, 8
    paddd   xmm3, xmm1
    movd    eax, xmm3
cl_tail:
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
    ret
_mp_count_lf endp

_mp_find3 proc
    push    esi
    mov     esi, [esp + 8]
    mov     edx, [esp + 12]
    mov     ecx, esi
    cmp     edx, 8
    jb      f3_tail
    movd    xmm0, dword ptr [esp + 16]
    pshuflw xmm0, xmm0, 0
    pshufd  xmm0, xmm0, 0
    movd    xmm1, dword ptr [esp + 20]
    pshuflw xmm1, xmm1, 0
    pshufd  xmm1, xmm1, 0
    movd    xmm2, dword ptr [esp + 24]
    pshuflw xmm2, xmm2, 0
    pshufd  xmm2, xmm2, 0
f3_vec:
    movdqu  xmm3, xmmword ptr [ecx]
    movdqa  xmm4, xmm3
    movdqa  xmm5, xmm3
    pcmpeqw xmm3, xmm0
    pcmpeqw xmm4, xmm1
    pcmpeqw xmm5, xmm2
    por     xmm3, xmm4
    por     xmm3, xmm5
    pmovmskb eax, xmm3
    test    eax, eax
    jnz     f3_hit
    add     ecx, 16
    sub     edx, 8
    cmp     edx, 8
    jae     f3_vec
f3_tail:
    test    edx, edx
    jz      f3_found
    movzx   eax, word ptr [ecx]
    cmp     eax, [esp + 16]
    je      f3_found
    cmp     eax, [esp + 20]
    je      f3_found
    cmp     eax, [esp + 24]
    je      f3_found
    add     ecx, 2
    dec     edx
    jmp     f3_tail
f3_hit:
    bsf     eax, eax
    add     ecx, eax
f3_found:
    mov     eax, ecx
    sub     eax, esi
    shr     eax, 1
    pop     esi
    ret
_mp_find3 endp

end

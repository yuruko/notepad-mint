; rt.asm - notepad mint runtime, hand-written 32-bit x86 (ml.exe / masm). no crt, no libs.
;
;   _memset _memcpy _memmove _memcmp   the compiler emits calls to these (cdecl)
;   __chkstk                           stack probe the compiler emits for frames >= one page
;   _mp_count_lf                       sse2 counter for 0x000a words (line numbers on big files)
;
; the process entry point is plain c (`start` in main.c), so nothing else lives here.
; cdecl: args on the stack, eax/ecx/edx/xmm0-7 are scratch, ebx/esi/edi/ebp are callee-saved.

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

; ---------------------------------------------------------------------------
; void *memset(void *dst, int c, size_t n)
; ---------------------------------------------------------------------------
_memset proc
    push    edi
    mov     edi, [esp + 8]          ; dst
    mov     eax, [esp + 12]         ; c (al)
    mov     ecx, [esp + 16]         ; n
    mov     edx, edi                ; return value
    rep     stosb
    mov     eax, edx
    pop     edi
    ret
_memset endp

; ---------------------------------------------------------------------------
; void *memcpy(void *dst, const void *src, size_t n)   (forward copy)
; ---------------------------------------------------------------------------
_memcpy proc
    push    esi
    push    edi
    mov     edi, [esp + 12]
    mov     esi, [esp + 16]
    mov     ecx, [esp + 20]
    mov     eax, edi                ; return value
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

; ---------------------------------------------------------------------------
; void *memmove(void *dst, const void *src, size_t n)   (overlap safe)
; ---------------------------------------------------------------------------
_memmove proc
    push    esi
    push    edi
    mov     edi, [esp + 12]
    mov     esi, [esp + 16]
    mov     ecx, [esp + 20]
    mov     eax, edi                ; return value
    cmp     edi, esi
    jbe     mm_fwd                  ; dst <= src: forward copy is always safe
    lea     edx, [esi + ecx]
    cmp     edi, edx
    jae     mm_fwd                  ; dst >= src+n: no overlap
    lea     edi, [edi + ecx - 1]    ; overlapping, dst above src: copy backwards: the last n mod 4 bytes, then dwords
    lea     esi, [esi + ecx - 1]
    mov     edx, ecx
    and     ecx, 3
    std
    rep     movsb
    sub     esi, 3                  ; (both now at the last byte of the last whole dword: its first byte)
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

; ---------------------------------------------------------------------------
; int memcmp(const void *a, const void *b, size_t n)
; ---------------------------------------------------------------------------
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
    jne     mc_diff                 ; a different dword: the byte loop below finds the first different byte of it
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

; ---------------------------------------------------------------------------
; __chkstk: eax = number of bytes the caller's frame needs. touches every new
; stack page in order so the guard page mechanism keeps working, then moves esp
; down by exactly eax bytes (the compiler addresses its locals relative to that)
; and returns to the caller. preserves everything except eax.
; ---------------------------------------------------------------------------
__chkstk proc
    push    ecx
    lea     ecx, [esp + 8]          ; esp as it was before the call
    sub     ecx, eax                ; where esp has to end up
    sbb     eax, eax                ; wrapped below zero? -> clamp to 0
    not     eax
    and     ecx, eax
    mov     eax, esp
    and     eax, 0FFFFF000h         ; page of the current stack top
cs_check:
    cmp     ecx, eax
    jb      cs_probe                ; target is below this page: touch the next one down
    mov     eax, ecx
    pop     ecx
    xchg    esp, eax                ; esp = target, eax = address of our return address
    mov     eax, [eax]
    push    eax                     ; return address again, one slot below the target...
    ret                             ; ...and ret pops it: esp ends up exactly at the target
cs_probe:
    sub     eax, 1000h
    test    dword ptr [eax], eax
    jmp     cs_check
__chkstk endp

; ---------------------------------------------------------------------------
; size_t mp_count_lf(const WCHAR *p, size_t n)
; number of 0x000a code units in p[0..n). 8 words per step via pcmpeqw+pmovmskb.
; (no popcnt: it isn't in baseline x86 - mask bits are counted with a clear-lowest-bit loop)
; ---------------------------------------------------------------------------
_mp_count_lf proc
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
_mp_count_lf endp

end

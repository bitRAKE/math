;	c:\fasm\fasm2\fasm2.cmd main.asm ..\verify.exe

; We make any assumption that helps simplify the code:
;	+ Single ABI frame, rest are leaf functions.
;	+ Input data is less than 1MB.
;	+ Data consists of lines starting: {#}, {#}.
;	- Of course, it can break easily, but also easy to fix.

include 'console.inc'

; 1MB Buffer + 64KB safety margin (shadow space, logic stack, etc.)
STACK_BUFFER_SIZE = 1024 * 1024 
STACK_TOTAL       = STACK_BUFFER_SIZE + 65536

; Request OS to reserve and commit this space immediately.
stack STACK_TOTAL, STACK_TOTAL

start:
	enter .frame, 0
	virtual at rbp + 16 ; parent shadow space
		.hStdOut	dq ?
		.hFile		dq ?
		.result		dd ?
		.bytesRead	dd ?

		assert $-$$ <= 32 ; shadow space limitation
	end virtual

	sub rsp, STACK_BUFFER_SIZE
	virtual at rsp + .frame
		.file_buffer rb STACK_BUFFER_SIZE
	end virtual

	mov [.result], 1 ; assume error

	invoke GetStdHandle, STD_OUTPUT_HANDLE
	mov [.hStdOut], rax

	invoke CreateFileA, 'km_plateaus.csv', GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0
	lea r8, [GLOB('Could not open file')]
	cmp rax, INVALID_HANDLE_VALUE
	jz .err
	mov [.hFile], rax

	lea rsi, [.file_buffer]
	invoke ReadFile, [.hFile], rsi, STACK_BUFFER_SIZE, addr .bytesRead, 0
	lea r8, [GLOB('ReadFile failed')]
	test eax, eax ; BOOL
	jz .err

	mov eax, [.bytesRead]
	lea r8, [GLOB('File too large for stack buffer')]
	cmp eax, STACK_BUFFER_SIZE
	jz .err

	lea r8, [GLOB('File empty')]
	test eax, eax
	jz .err
	mov qword [rsi + rax], 0 ; null-terminate the buffer

	invoke CloseHandle, [.hFile]

; Line reader loop:
;   + any line not starting with two comma-separated numbers is ignored
;   + bytes afterward [until the end of line] are ignored
;   + whitespace/comma around numbers is ignored
.parse_loop:
	call parse_uint			; parse k
	jc .skip_line
	mov r12, rcx

	call parse_uint			; parse m
	jc .skip_line
	mov r13, rcx

	call verify_sequence		; verify (R12=k, R13=m)
	jnc .parse_loop

.verify_fail:
	invoke wsprintfA, addr .file_buffer, addr _t_fail, r12, r13, r14
	jmp .out

.err:	invoke wsprintfA, addr .file_buffer, addr _t_err, r8
	jmp .out

; If parse failed (e.g., header row), skip to next line (any newline arrangement okay):
.skip_line:
	lodsb
	cmp al, 13		; CR
	jz .parse_loop
	cmp al, 10		; LF
	jz .parse_loop
	test al, al
	jnz .skip_line

.done_all:
	mov [.result], 0 ; success
	invoke wsprintfA, addr .file_buffer, addr _t_str, 'All values check out'

.out:	xchg r8d, eax
	invoke WriteFile, [.hStdOut], addr .file_buffer, r8, 0, 0
	invoke ExitProcess, [.result]

.frame := fastcall.frame ; ABI required parameter space


_t_fail	GLOBSTR 'FAIL: k=%d, m=%d. Failed at offset %d.',13,10,0
_t_err	GLOBSTR 'Error: %s.',13,10,0
_t_str	GLOBSTR '%s.',13,10,0

;=============================================================| Leaf Functions:

; Very loose parsing: tab, spaces and comma can be skipped around digits:
parse_uint:
	xor eax, eax
	xor ecx, ecx
.ctrl:	lodsb
	test al, al
	jz .err
	cmp al, ' '+1
	jc .ctrl		; Y: consume ALL control chars and space
	cmp al, ','
	jz .ctrl		; Y: commas are also consumed

; At this point a non-digit is an error.

	sub al, '0'
	cmp al, 10
	jnc .err

.more:	imul rcx, rcx, 10
	add rcx, rax

; At this point a non-digit signals end of number

	lodsb
	sub al, '0'
	cmp al, 10
	jc .more
	dec rsi			; restore unprocessed character
	retn

.err:	dec rsi			; restore unprocessed character
	stc			; not-a-number, probably header/comment/eof
	retn


; =============================================================================
; Verification Logic (Stop-at-K Algorithm)
; =============================================================================
verify_sequence:
    xor     r14, r14                ; i = 0
.loop:
    inc     r14                     ; i++
    cmp     r14, r12
    jg      .success                ; i > k, sequence passed

    mov     rax, r13
    add     rax, r14                ; RAX = N = m + i

    ; --- Optimize: Check 2 ---
    test    al, 1
    jnz     .check_odd
    cmp     r12, 2                  ; If k < 2, then 2 is a large prime.
    jl      .num_ok
.strip_2:
    shr     rax, 1
    test    al, 1
    jz      .strip_2
    cmp     rax, 1
    je      .fail
.check_odd:
    mov     rbx, 3
.factor:
    cmp     rbx, r12
    jg      .check_rem              ; Divisors > k, stop checking.

    ; Optimize: Divisor^2 > N check
    mov     r8, rax                 ; Save N
    xor     rdx, rdx
    div     rbx                     ; RAX=q, RDX=r
    
    cmp     rax, rbx
    jl      .prime_rem              ; q < div => div > sqrt(N)

    test    rdx, rdx
    jz      .divisible

    mov     rax, r8                 ; Restore N
    add     rbx, 2
    jmp     .factor

.divisible:
    cmp     rax, 1                  ; Reduced to 1? Fail.
    je      .fail
    
    ; Keep dividing by rbx
    mov     r8, rax
    xor     rdx, rdx
    div     rbx
    test    rdx, rdx
    jz      .divisible
    
    mov     rax, r8
    add     rbx, 2
    jmp     .factor

.check_rem:
    cmp     rax, 1                  ; Remainder > 1? Success.
    jg      .num_ok
    jmp     .fail

.prime_rem:
    ; r8 was prime. Check if > k.
    cmp     r8, r12
    jg      .num_ok
    jmp     .fail

.num_ok:
	jmp .loop

.success:
	clc
	retn

.fail:
	stc
	retn

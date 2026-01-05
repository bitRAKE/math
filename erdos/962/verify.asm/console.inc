format PE64 NX CONSOLE 5.0
entry start

include 'win64a.inc'
include 'globstr.inc' ; constant byte string support
include 'macro/inline.inc'

GLOBSTR.reuse := 1 ; turn on substring matching

; Replace the "fastcall?.inline_string" macro to prevent constant string data from being emitted in the code stream. Instead the string arguments to "fastcall" and "invoke" are handled by:
macro fastcall?.inline_string var
	local data
	data GLOBSTR var,0
	redefine var data
end macro

; Inline macro allows to put string data in any expression (getting the address in return):
inlinemacro GLOB(value)
	local data
	data GLOBSTR value,0
	return equ data
end inlinemacro

;section '.const' data readable
;	GLOBSTR.here

section '.text' code readable executable

fastcall.frame = 0 ; Setting fastcall.frame to non-negative value makes fastcall/invoke use it to track maximum necessary stack space (aligned to a multiple of 16 bytes), and not allocate it automatically.

postpone

section '.rdata' data readable
	GLOBSTR.here

	align 8
	data import
		library\
		kernel32,'KERNEL32.DLL',\
		user32,'USER32.DLL'

		include 'api/kernel32.inc'
		include 'api/user32.inc'
	end data
end postpone

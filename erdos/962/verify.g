;	fasmg.exe verify.g

fail = 1
fail = 0

calminstruction verify_sequence k*, m*
    local i, current, temp, divisor
    
    ; Initialize loop counter i = 0
    compute i, 0

    next_number_success:
    loop_range:
        compute i, i + 1
        check i <= k ; If i > k, we have successfully verified the whole range
        jno success_end

        compute current, m + i
        compute temp, current
        compute divisor, 2

    loop_factor:
	check divisor > k
	jyes check_remainder_large
	check divisor * divisor > temp
	jyes check_remainder_prime

        check temp mod divisor = 0
        jyes is_divisible
        compute divisor, divisor + 1
        jump loop_factor

    is_divisible:
        check divisor > k
        jyes next_number_success ; requirement met for this number
        compute temp, temp / divisor
        jump loop_factor

check_remainder_large:
	check temp > 1
	jyes next_number_success
	jump verification_failed

check_remainder_prime:
	check temp > k
	jyes next_number_success
	jump verification_failed

    verification_failed:
	compute fail,1 ; set global fail flag

	display 'Verification failed for: '
	compute temp,0+k
	arrange temp,temp
	stringify temp
	display temp
	compute temp,0+m
	arrange temp,temp
	stringify temp
	display ',' bappend temp
	display 10
    success_end:
end calminstruction

calminstruction reader?! &line& ; unconditional processing
	match ,line
	jyes done
	match =mvmacro? =reader? any?,line
	jno do
	assemble line
	check fail
	jyes done
	display 'All values check out.'
done:	exit

	local k_num,m_num,any,head,rest
do:
	check fail ; bypass after first error
	jyes done

; ignore anything that doesn't start with two numbers
; allow anything following numbers: note/comment/etc

	match k_num =, m_num any?,line
	jno unknown

	check definite k_num	\
	& k_num eqtype 0	\
	& k_num relativeto 0	\
	& definite m_num	\
	& m_num eqtype 0	\
	& m_num relativeto 0
	jno unknown

; verify that m+1,...,m+k are all divisible by at least one prime > k
	call verify_sequence,k_num,m_num

	exit

unknown:
	stringify line
	display line
	display 10
end calminstruction
include 'km_plateaus.csv',mvmacro ?,reader?
mvmacro reader?,?

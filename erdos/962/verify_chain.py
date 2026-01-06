import sys
import time

def get_prime_factors_map(n):
    """
    Returns a set of prime factors for n. 
    Not efficient for huge numbers, but we only care about small factors 
    up to the plateau size (e.g. ~5000), so we stop looking early.
    """
    factors = set()
    d = 2
    temp = n
    # We only strictly need factors up to ~5000 (max k)
    # But for correctness, we just strip what we find.
    while d * d <= temp:
        if temp % d == 0:
            factors.add(d)
            while temp % d == 0:
                temp //= d
        d += 1
    if temp > 1:
        factors.add(temp)
    return factors

def is_prime(n):
    if n < 2: return False
    if n == 2: return True
    if n % 2 == 0: return False
    d = 3
    while d * d <= n:
        if n % d == 0: return False
        d += 2
    return True

def measure_true_length(k_start, m):
    """
    Determines the maximum 'z' such that for all i in 1..z:
      P(m+i) > z
    
    Logic mirrors the assembly:
    1. Initialize array of values [m+1 ... m+limit]
    2. Strip factors <= k_start
    3. Incrementally increase z, stripping new prime factors as z grows.
    """
    
    # Heuristic: We don't expect the true length to be MASSIVELY larger 
    # than k in this specific dataset context, but allow some headroom.
    # If the plateau extends further, we'd need a dynamic array, but 
    # for verification this fixed buffer is usually sufficient.
    limit = k_start + 2000 
    
    # residues[i] corresponds to m + i (1-based index)
    # We essentially track the "remaining part" of the number after division.
    # index 0 is unused to match 1-based logic.
    residues = [0] * (limit + 1)
    
    # 1. Init and pre-strip factors <= k_start
    for i in range(1, limit + 1):
        val = m + i
        # Strip all primes <= k_start
        # (In Python this is slow to do fully, so we assume the input k is valid
        # and just strip factors that are <= k_start).
        # Optimization: We only need to check divisibility by primes.
        
        # However, to be robust, let's just use the 'val' and reduce it
        # dynamically in the loop below.
        residues[i] = val

    # To be efficient in Python, we simulate the Sieve:
    # Iterate p from 2 to k_start: divide out p from the whole array.
    for p in range(2, k_start + 1):
        if not is_prime(p): continue
        
        # Start at first multiple of p > m
        # first index i such that m+i is divisible by p
        # (m+i) % p == 0  =>  i % p == (-m) % p
        rem = (-m) % p
        if rem == 0: rem = p
        
        for i in range(rem, limit + 1, p):
            while residues[i] % p == 0:
                residues[i] //= p

    # 2. Check base validity (1..k_start)
    for i in range(1, k_start + 1):
        if residues[i] == 1:
            return -1 # Should not happen if CSV is valid

    # 3. Extend z
    current_z = k_start
    
    while True:
        next_z = current_z + 1
        if next_z > limit:
            return -2 # Buffer limit reached (plateau is huge!)

        # A. If next_z is prime, strip it from previous residues 1..current_z
        if is_prime(next_z):
            rem = (-m) % next_z
            if rem == 0: rem = next_z
            
            # We only care about indices <= current_z for the "existing" plateau validity
            # But we must also strip it from future indices to prepare them.
            # So strip from valid range...
            for i in range(rem, limit + 1, next_z):
                while residues[i] % next_z == 0:
                    residues[i] //= next_z
                
                # If we stripped it from an index within the active plateau range
                # and it became 1, the plateau breaks *at that index*.
                # But wait: the condition is P(m+i) > next_z.
                # If residue became 1, it means all factors were <= next_z.
                # So P(m+i) <= next_z. FAIL condition met.
                if i <= current_z and residues[i] == 1:
                    return current_z # The expansion failed, true size is current_z

        # B. Check the new item at index next_z
        # It has been stripped of all primes <= next_z (by the loop above and previous steps).
        # If it is 1, it fails.
        if residues[next_z] == 1:
            return current_z

        current_z = next_z

def main():
    filename = 'km_plateaus.csv'
    if len(sys.argv) > 1:
        filename = sys.argv[1]

    print(f"Verifying chain coverage in {filename}...")
    
    data = []
    try:
        with open(filename, 'r') as f:
            for line in f:
                parts = line.split(',')
                if len(parts) >= 2 and parts[0].strip().isdigit():
                    k = int(parts[0])
                    m = int(parts[1])
                    data.append((k, m))
    except FileNotFoundError:
        print("File not found.")
        return

    print(f"Loaded {len(data)} records.")
    print("-" * 60)
    print(f"{'Row K':<10} | {'True Length':<15} | {'Next K':<10} | {'Status'}")
    print("-" * 60)

    start_time = time.time()
    gaps = 0

    for i in range(len(data)):
        k, m = data[i]
        
        # Calculate how far this plateau *really* goes
        true_len = measure_true_length(k, m)
        
        if true_len == -1:
            print(f"{k:<10} | {'INVALID':<15} | {'-':<10} | FAIL (Base k invalid)")
            return
        
        # Determine what the next k is (if it exists)
        next_k_str = "END"
        status = "OK"
        
        if i < len(data) - 1:
            next_k = data[i+1][0]
            next_k_str = str(next_k)
            
            # Logic: 
            # If True Length is 100, the plateau covers 1..100.
            # The next plateau MUST start searching at coverage 101.
            # So ideally Next K == True Length + 1.
            if next_k == true_len + 1:
                status = "PERFECT"
            elif next_k <= true_len:
                status = "OVERLAP" # Not an error, just redundant coverage
            else:
                status = f"GAP ({next_k - true_len - 1})"
                gaps += 1
        else:
            status = "FINAL"

        # Only print gaps or every 500th line to avoid spamming console
        if status.startswith("GAP") or i % 500 == 0 or i == len(data)-1:
            print(f"{k:<10} | {true_len:<15} | {next_k_str:<10} | {status}")

    duration = time.time() - start_time
    print("-" * 60)
    if gaps == 0:
        print(f"Chain Verified: Complete continuous coverage found.")
    else:
        print(f"Verification Complete: {gaps} gaps found.")
    print(f"Time: {duration:.2f}s")

if __name__ == "__main__":
    main()

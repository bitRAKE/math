import csv
import sys
import time

def verify_sequence(k, m):
    """
    Verifies that for every number n in [m+1, m+k], 
    there exists a prime factor p such that p > k.
    """
    # Check every number in the range
    for i in range(1, k + 1):
        num = m + i
        temp = num
        
        # We don't need to fully factorize. We just need to strip out
        # all prime factors <= k.
        
        # 1. Handle 2 separately for speed
        while temp % 2 == 0:
            temp //= 2
            
        # 2. Handle odd factors up to k
        # We only need to check up to min(k, sqrt(temp)) actually, 
        # but stopping at k is the logic we used in fasmg.
        d = 3
        while d <= k and d * d <= temp:
            while temp % d == 0:
                temp //= d
            d += 2
            
        # 3. Decision time
        # If temp > 1 here, it means the remaining part of the number 
        # is composed of prime factors strictly greater than the divisors we checked.
        # Since we checked up to 'k' (or until temp became small), 
        # any remainder must be a prime > k.
        
        # However, we must be careful: if we stopped the loop because d*d > temp,
        # we might still have a small prime remaining if temp <= k.
        # So explicitly: is the remainder > k?
        
        # Note: If we fully stripped all factors <= k, then any remaining 'temp' > 1
        # MUST be a prime > k.
        # But if we broke early (d > k), we haven't stripped factors > k yet.
        
        pass_check = False
        
        # If we reduced temp to 1, it means ALL factors were <= k. (Fail for this number)
        if temp == 1:
            pass_check = False
        
        # If temp > k, then temp is a prime > k (or contains one). (Success)
        elif temp > k:
            pass_check = True
            
        # If 1 < temp <= k, it means the remaining factor is a prime <= k. (Fail)
        else:
            pass_check = False
            
        if not pass_check:
            return False, num

    return True, None

def main():
    filename = 'km_plateaus.csv'
    
    print(f"Checking {filename}...")
    start_time = time.time()
    
    count = 0
    failures = 0
    
    try:
        with open(filename, 'r') as f:
            # Handle potential header if it exists
            lines = f.readlines()

        for line in lines:
            line = line.strip()
            # Skip empty lines or headers that don't start with a digit
            if not line or not line[0].isdigit():
                continue
                
            parts = line.split(',')
            if len(parts) < 2:
                continue
                
            try:
                k = int(parts[0])
                m = int(parts[1])
            except ValueError:
                continue
                
            count += 1

            if k == 1 and m == 1:
                continue

            success, bad_num = verify_sequence(k, m)
            
            if not success:
                print(f"FAIL: k={k}, m={m}. Failed at {bad_num}")
                failures += 1
                # Optional: break after first failure
                # break 
                
    except FileNotFoundError:
        print(f"Error: Could not find {filename}")
        return

    end_time = time.time()
    duration = end_time - start_time
    
    print("-" * 30)
    print(f"Processed {count} pairs in {duration:.2f} seconds.")
    if failures == 0:
        print("All values check out.")
    else:
        print(f"Found {failures} failures.")

if __name__ == "__main__":
    main()

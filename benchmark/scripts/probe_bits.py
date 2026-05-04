import mmap


def gospers_hack(n : int, k : int) -> list[int]:
    """
    Generates all n-bit integers with exactly k set bits.
    """
    if k == 0:
        return [0]
    if k > n:
        return []
    

    # Initialize x with the smallest integer having k set bits
    results = []
    x = (1 << k) - 1
    limit = 1 << n
    
    while x < limit:
        # Format as binary for display purposes
        # results.append(f"{x:0{n}b} (decimal {x})")
        results.append(x)
        
        # --- The Hack ---
        lowbit = x & -x                # Identify the lowest set bit
        left = x + lowbit              # Add it to x to flip the lowest block of 1s
        # Extract the flipped block, shift it to the right, 
        # and adjust its size to maintain exactly k bits.
        right = ((x ^ left) >> 2) // lowbit
        x = left | right               # Combine for the next value
        
    return results




if __name__ == "__main__":
    shm = mmap.mmap(0, 1024**2 * 64, )


    # Example: n=5, k=3
    n_val, k_val = 30, 15
    output = gospers_hack(n_val, k_val)

    print(f"output has {len(output)} parameters")
    # for val in output:
    #     print(val)
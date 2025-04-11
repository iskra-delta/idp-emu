# font_viewer.py
filename = "charset_ef9365.rom"  # Replace with your file name
bytes_per_char = 5
rows_per_char = 7
bits_per_row = 5

with open(filename, "rb") as f:
    data = f.read()

for char_idx in range(0, len(data), bytes_per_char):
    char_data = data[char_idx:char_idx + bytes_per_char]
    print(f"Character {char_idx // bytes_per_char}:")
    # Unpack 35 bits (5 bytes) into 7 rows of 5 bits
    bits = ""
    for byte in char_data:
        bits += format(byte, "08b")  # Get 8 bits per byte
    bits = bits[:35]  # Take only 35 bits
    for row in range(rows_per_char):
        start = row * bits_per_row
        row_bits = bits[start:start + bits_per_row]
        if len(row_bits) < bits_per_row:
            row_bits += "0" * (bits_per_row - len(row_bits))  # Pad if needed
        print(row_bits.replace("1", "█").replace("0", " "))
    print()
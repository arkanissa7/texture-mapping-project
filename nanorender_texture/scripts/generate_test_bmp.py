import struct

# Create a minimal 2x2 24-bit BMP texture.
# Bottom row: red, green.
# Top row: blue, white.

def write_bmp(path):
    width = 2
    height = 2
    bytes_per_pixel = 3
    row_size = (width * bytes_per_pixel + 3) // 4 * 4
    image_size = row_size * height
    file_size = 54 + image_size

    header = b'BM'
    header += struct.pack('<IHHI', file_size, 0, 0, 54)
    header += struct.pack('<IIIHHIIIIII', 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0)

    pixels = [
        (0, 0, 255),  # Blue
        (255, 255, 255),  # White
        (255, 0, 0),  # Red
        (0, 255, 0),  # Green
    ]

    rows = []
    rows.append(b''.join(struct.pack('BBB', *pixels[2 + x]) for x in range(width)))
    rows.append(b''.join(struct.pack('BBB', *pixels[x]) for x in range(width)))

    padded_rows = [row + b'\x00' * (row_size - len(row)) for row in rows]

    with open(path, 'wb') as f:
        f.write(header)
        for row in padded_rows:
            f.write(row)

    print(f'Wrote {path}')

if __name__ == '__main__':
    write_bmp('../textures/simple.bmp')
    write_bmp('../textures/cube.bmp')

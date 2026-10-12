# Q5: Embedded Firmware Log Compression Utility

Compresses `device_log.txt` with Huffman coding, restores it from `device_log.huff`, and checks that the restored file matches the original byte for byte.

## Build and run

```sh
cc -std=c11 -Wall -Wextra -pedantic -o log_compressor log_compressor.c
./log_compressor
```

## Menu

| Option | Operation |
|---|---|
| 1 | Compress `device_log.txt` → `device_log.huff` |
| 2 | Decompress `device_log.huff` → `device_log_restored.txt`, then verify automatically |
| 3 | Verify that the restored file matches the original |
| 4 | Exit |

## Compressed file format

```
"HUF1" | original size (8 bytes, little-endian) | bitstream
```

The bitstream begins with the Huffman tree in pre-order: `0` marks an internal node, and `1` followed by 8 bits marks a leaf and its character. The encoded data follows. This lets the decompressor rebuild the exact tree without the frequency table.

## Design notes

- The file is read in binary mode, so every byte (newlines included) is counted and restored.
- The tree is built with a min-heap. When two weights are equal, a rank value decides the order, so the same input always produces the same codes.
- Codes come from a recursive walk of the tree (left = `0`, right = `1`). A file with only one distinct character still gets the code `0`.
- The decoder stops after the original byte count, so the padding bits in the last byte are ignored.
- Verification compares the two files in blocks and reports the first byte that differs.

## Sample result

The included `device_log.txt` holds 24 periodic readings from device `RWA_014`:

```
Original size:     2984 bytes
Compressed size:   1819 bytes
Compression ratio: 60.96% (space saved: 39.04%)
Verification: SUCCESS
```

## Tested scenarios

- Lossless round trip on the sample log, a one-character file, an empty file, and 200 KB of random binary data.
- A truncated or invalid `.huff` file, and a missing input or compressed file.
- A tampered restored file is reported as `Verification: FAILED`.
- Invalid menu input.
- Checked with AddressSanitizer: no leaks or invalid memory access.

# Lossless File Compressor (Huffman Coding in C++)

A high-performance, command-line lossless file compressor and decompressor written in C++17. It utilizes the **Huffman Coding** algorithm to achieve entropy-based data compression. 

The project has **zero external dependencies** and uses only standard C++ library headers.

---

## Key Features

* **True Bit-Packing Logic**: Performs actual bit-level operations (`<<`, `|`, `&`) to pack 8 bits into a single byte (`unsigned char`) before writing to disk, avoiding slow and space-inefficient string representations of `"0"` and `"1"`.
* **Automatic "Store Mode" Fallback**: Solves the "small files grow larger" problem. If the overhead of storing the Huffman frequency table exceeds the potential savings (common in tiny or highly random files), the compressor falls back to storing the raw file as-is with a single flag byte, guaranteeing the file never grows.
* **Deterministic Tie-Breaking (Stable Tree Construction)**: Nodes with equal frequencies are ordered using sequential unique IDs. This guarantees that the compression and decompression phases construct the exact same binary tree, regardless of compile-time memory layouts or platform-specific sorting differences.
* **Header & Padding Management**: Prepends the original file size to the compressed file. The decompressor decodes exactly this number of bytes and stops, seamlessly ignoring trailing padding bits in the final byte.
* **Leak-Free Memory Management**: Destructors recursively traverse and delete tree nodes, ensuring zero heap memory leaks.

---

## File Format Architecture

The compressed `.bin`/`.huf` file uses a custom structure defined as follows:

| Field | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| **Mode Flag** | 1 byte | `uint8_t` | `0` = Stored raw (uncompressed), `1` = Huffman compressed |
| **Original Size** | 8 bytes | `uint64_t` | Total count of characters in the uncompressed file (Only in Mode 1) |
| **Alphabet Size** | 2 bytes | `uint16_t` | Number of unique characters in the frequency table (Only in Mode 1) |
| **Frequency Entry** | 9 bytes each | `char` + `uint64_t` | Pair of `(character, frequency)` for every unique character (Only in Mode 1) |
| **Bitstream** | Variable | Packed bits | The actual Huffman-coded bitstream packed 8 bits/byte (Only in Mode 1) |

---

## How It Works

1. **Analysis**: The compressor reads the input file and calculates byte frequencies.
2. **Evaluation**: It builds the Huffman tree and estimates the compressed output size (header + bitstream bytes).
3. **Decision**:
   * If **Compressed Size < Original Size**: Writes Mode Flag `1`, serializes the header, and compresses the file using packed bits.
   * If **Compressed Size >= Original Size**: Writes Mode Flag `0` and copies the original file bytes directly.
4. **Decompression**: The decompressor reads the first byte. If `0`, it copies the raw data. If `1`, it reads the frequency table, rebuilds the identical tree, and decodes the packed bits using a bit-by-bit reader.

---

## Benchmark & Experimental Results

Below are the benchmark results measured on various test files:

| Test Case | Original Size | Compressed Size | Mode | Compression Ratio | Integrity Check |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Empty File** | 0 bytes | 1 byte | **STORE** (Raw) | N/A | **PASS** |
| **Standard Text** | 306 bytes | 307 bytes | **STORE** (Raw) | 100.33% | **PASS** |
| **Binary Data** | 2,560 bytes | 2,561 bytes | **STORE** (Raw) | 100.04% | **PASS** |
| **Single Character Repeating** | 10,000 bytes | 1,270 bytes | **COMPRESS** (Huffman) | **12.70%** (87.3% saved) | **PASS** |
| **Large Text File (1 MB)** | 1,074,500 bytes | 626,907 bytes | **COMPRESS** (Huffman) | **58.34%** (41.66% saved) | **PASS** |

---

## Getting Started

### Prerequisites
* A C++17 compatible compiler (e.g., `g++` 7.0+, `clang++` 5.0+, or MSVC 2017+).
* PowerShell (optional, for running automated test scripts).

### Compilation
Compile the source using your C++ compiler. Optimizations (`-O3`) are highly recommended for faster I/O processing:
```bash
g++ -O3 -Wall -std=c++17 main.cpp -o compressor

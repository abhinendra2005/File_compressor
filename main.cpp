/**
 * Huffman Coding Lossless File Compressor with 'Store Mode' Fallback
 * 
 * Explanation of Bit-Packing and Unpacking Logic:
 * 
 * 1. Bit-Packing (BitWriter):
 *    - To write compressed data, we map each source character to a variable-length Huffman code (a string of '0' and '1' characters).
 *    - Instead of writing these characters directly to disk as text bytes, we pack the bits
 *      into bytes (unsigned char) using bitwise operators:
 *      - We maintain a single-byte buffer (`uint8_t buffer`) and a counter (`bitCount`) of bits written to the buffer.
 *      - For each bit '0' or '1', we shift the buffer left by 1 (`buffer << 1`) and bitwise-OR the new bit (`buffer | bit`).
 *      - Once `bitCount` reaches 8, the buffer is full and we write it to the binary output stream using `out.put()`,
 *        then reset `buffer` and `bitCount` to 0.
 *      - When the file ends, if we have a partially filled buffer (i.e. `bitCount > 0`), we pad the remaining bits with
 *        zeros by shifting the buffer left by `8 - bitCount` positions, write the final byte, and flush the stream.
 * 
 * 2. Unpacking (BitReader):
 *    - To decompress, we read the packed bytes back from the file one byte at a time and extract individual bits:
 *      - We maintain a byte buffer (`uint8_t buffer`) and a counter (`bitCount`) representing the number of unread bits in the buffer.
 *      - When `bitCount` is 0, we read the next byte from the stream, fill the buffer, and set `bitCount` to 8.
 *      - To read a bit, we extract the Most Significant Bit (MSB) by shifting the buffer right by 7 and bitwise-ANDing with 1: `(buffer >> 7) & 1`.
 *      - We then shift the buffer left by 1 (`buffer <<= 1`) to position the next bit at the MSB, and decrement `bitCount`.
 * 
 * 3. Handling Padding (Original File Size Tracking):
 *    - Since the last byte of the compressed file may be padded with dummy zeros, reading bits blindly until the end of the file
 *      would result in decoding extra dummy characters.
 *    - To prevent this, the compressor writes the exact total number of characters (`original_size`) in the file header.
 *    - The decompressor decodes exactly `original_size` characters by traversing the tree and then stops, ignoring any
 *      remaining padding bits in the final byte.
 * 
 * 4. Store Mode Fallback:
 *    - To handle small files, we write a 1-byte mode flag at the beginning of the compressed file:
 *      - Flag `0` (STORE): The file is written as-is without compression. Useful if header overhead > original size.
 *      - Flag `1` (COMPRESS): The file is compressed using Huffman coding.
 */

#include <iostream>
#include <fstream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

// Huffman Node structure
struct HuffmanNode {
    uint8_t character;
    uint64_t frequency;
    uint32_t id; // Deterministic tie-breaker for priority queue sorting
    HuffmanNode* left;
    HuffmanNode* right;

    // Leaf node constructor
    HuffmanNode(uint8_t ch, uint64_t freq)
        : character(ch), frequency(freq), id(ch), left(nullptr), right(nullptr) {}

    // Internal node constructor
    HuffmanNode(uint64_t freq, uint32_t nodeId, HuffmanNode* l, HuffmanNode* r)
        : character(0), frequency(freq), id(nodeId), left(l), right(r) {}

    // Recursive destructor to prevent memory leaks
    ~HuffmanNode() {
        delete left;
        delete right;
    }
};

// Functor for ordering the priority queue (min-heap)
struct CompareNodes {
    bool operator()(const HuffmanNode* lhs, const HuffmanNode* rhs) const {
        if (lhs->frequency != rhs->frequency) {
            return lhs->frequency > rhs->frequency;
        }
        return lhs->id > rhs->id; // Smaller ID has higher priority
    }
};

// Helper class for writing individual bits to a stream
class BitWriter {
private:
    std::ostream& out;
    uint8_t buffer;
    int bitCount;

public:
    explicit BitWriter(std::ostream& os) : out(os), buffer(0), bitCount(0) {}

    // Writes a single bit (0 or 1) to the buffer
    void writeBit(int bit) {
        buffer = (buffer << 1) | (bit & 1);
        bitCount++;
        if (bitCount == 8) {
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }

    // Writes a string of bit characters ('0' or '1')
    void writeString(const std::string& bitStr) {
        for (char c : bitStr) {
            writeBit(c - '0');
        }
    }

    // Pads any remaining bits in the buffer with zeros and writes it out
    void flush() {
        if (bitCount > 0) {
            buffer <<= (8 - bitCount);
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }
};

// Helper class for reading individual bits from a stream
class BitReader {
private:
    std::istream& in;
    uint8_t buffer;
    int bitCount;

public:
    explicit BitReader(std::istream& is) : in(is), buffer(0), bitCount(0) {}

    // Reads a single bit from the buffer. Returns 0 or 1, or -1 on EOF/error.
    int readBit() {
        if (bitCount == 0) {
            char ch;
            if (!in.get(ch)) {
                return -1; // EOF or error
            }
            buffer = static_cast<uint8_t>(ch);
            bitCount = 8;
        }
        int bit = (buffer >> 7) & 1;
        buffer <<= 1;
        bitCount--;
        return bit;
    }
};

// Main Compressor class
class HuffmanCompressor {
private:
    // Helper to build the Huffman tree from a frequency map
    HuffmanNode* buildTree(const std::unordered_map<uint8_t, uint64_t>& freqs) {
        if (freqs.empty()) {
            return nullptr;
        }

        std::priority_queue<HuffmanNode*, std::vector<HuffmanNode*>, CompareNodes> pq;
        for (const auto& pair : freqs) {
            pq.push(new HuffmanNode(pair.first, pair.second));
        }

        // Edge case: File containing only one unique character
        if (pq.size() == 1) {
            HuffmanNode* onlyNode = pq.top();
            pq.pop();
            // Create a dummy node so the leaf is at depth 1 (gets code '0')
            HuffmanNode* dummy = new HuffmanNode(0, 0);
            HuffmanNode* root = new HuffmanNode(onlyNode->frequency, 256, onlyNode, dummy);
            return root;
        }

        // Standard Huffman tree construction
        uint32_t nextId = 256;
        while (pq.size() > 1) {
            HuffmanNode* left = pq.top(); pq.pop();
            HuffmanNode* right = pq.top(); pq.pop();
            HuffmanNode* parent = new HuffmanNode(left->frequency + right->frequency, nextId++, left, right);
            pq.push(parent);
        }

        return pq.top();
    }

    // Helper to traverse the tree and collect codes
    void generateCodes(HuffmanNode* node, const std::string& code, std::unordered_map<uint8_t, std::string>& codes) {
        if (!node) return;
        if (!node->left && !node->right) {
            codes[node->character] = code;
            return;
        }
        generateCodes(node->left, code + "0", codes);
        generateCodes(node->right, code + "1", codes);
    }

public:
    // Compresses input file and writes to output file
    bool compress(const std::string& inputPath, const std::string& outputPath) {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) {
            std::cerr << "Error: Cannot open input file: " << inputPath << std::endl;
            return false;
        }

        // 1. Calculate frequencies and original file size
        std::unordered_map<uint8_t, uint64_t> freqs;
        uint64_t original_size = 0;
        char ch;
        while (in.get(ch)) {
            freqs[static_cast<uint8_t>(ch)]++;
            original_size++;
        }
        in.close();

        // 2. Estimate compressed file size
        uint64_t total_bits = 0;
        HuffmanNode* root = nullptr;
        std::unordered_map<uint8_t, std::string> codes;

        if (original_size > 0) {
            root = buildTree(freqs);
            generateCodes(root, "", codes);
            for (const auto& pair : freqs) {
                total_bits += pair.second * codes[pair.first].length();
            }
        }

        uint64_t compressed_data_bytes = (total_bits + 7) / 8;
        uint64_t header_bytes = 8 + 2 + freqs.size() * 9;
        uint64_t total_compressed_bytes = 1 + header_bytes + compressed_data_bytes; // 1 byte for mode flag

        // 3. Open output file in binary write mode
        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            std::cerr << "Error: Cannot open output file: " << outputPath << std::endl;
            delete root;
            return false;
        }

        // 4. Check if Huffman coding actually reduces size
        if (original_size == 0 || total_compressed_bytes >= original_size) {
            // Write Mode Flag: 0 (STORE - uncompressed)
            uint8_t mode = 0;
            out.put(static_cast<char>(mode));

            // Copy raw file contents if it's not empty
            if (original_size > 0) {
                in.open(inputPath, std::ios::binary);
                if (!in) {
                    std::cerr << "Error: Cannot re-open input file: " << inputPath << std::endl;
                    delete root;
                    return false;
                }
                out << in.rdbuf();
                in.close();
            }
            out.close();
            delete root;
            std::cout << "Stored uncompressed (Header overhead too high)." << std::endl;
            return true;
        }

        // Write Mode Flag: 1 (COMPRESS - Huffman coding)
        uint8_t mode = 1;
        out.put(static_cast<char>(mode));

        // Write original size (8 bytes)
        out.write(reinterpret_cast<const char*>(&original_size), sizeof(original_size));

        // Write alphabet size (2 bytes)
        uint16_t alphabet_size = static_cast<uint16_t>(freqs.size());
        out.write(reinterpret_cast<const char*>(&alphabet_size), sizeof(alphabet_size));

        // Write frequency table (each entry: 1 byte character, 8 bytes frequency)
        for (const auto& pair : freqs) {
            uint8_t character = pair.first;
            uint64_t frequency = pair.second;
            out.write(reinterpret_cast<const char*>(&character), sizeof(character));
            out.write(reinterpret_cast<const char*>(&frequency), sizeof(frequency));
        }

        // Re-open input file to compress and pack the bits
        in.open(inputPath, std::ios::binary);
        if (!in) {
            std::cerr << "Error: Cannot re-open input file for packing: " << inputPath << std::endl;
            delete root;
            return false;
        }

        BitWriter writer(out);
        while (in.get(ch)) {
            uint8_t u_ch = static_cast<uint8_t>(ch);
            writer.writeString(codes[u_ch]);
        }

        writer.flush();

        in.close();
        out.close();
        delete root;
        std::cout << "Stored using Huffman compression." << std::endl;
        return true;
    }

    // Decompresses input file and writes to output file
    bool decompress(const std::string& inputPath, const std::string& outputPath) {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) {
            std::cerr << "Error: Cannot open input file: " << inputPath << std::endl;
            return false;
        }

        // Read Mode Flag
        char mode_char;
        if (!in.get(mode_char)) {
            std::cerr << "Error: Failed to read mode flag." << std::endl;
            return false;
        }
        uint8_t mode = static_cast<uint8_t>(mode_char);

        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            std::cerr << "Error: Cannot open output file: " << outputPath << std::endl;
            return false;
        }

        // Mode 0: Uncompressed copy
        if (mode == 0) {
            out << in.rdbuf();
            in.close();
            out.close();
            return true;
        }

        // Mode 1: Huffman decompression
        if (mode != 1) {
            std::cerr << "Error: Unknown mode flag in compressed file." << std::endl;
            return false;
        }

        // 1. Read header
        uint64_t original_size = 0;
        if (!in.read(reinterpret_cast<char*>(&original_size), sizeof(original_size))) {
            std::cerr << "Error: Failed to read original size from header." << std::endl;
            return false;
        }

        uint16_t alphabet_size = 0;
        if (!in.read(reinterpret_cast<char*>(&alphabet_size), sizeof(alphabet_size))) {
            std::cerr << "Error: Failed to read alphabet size from header." << std::endl;
            return false;
        }

        // Read frequency table entries
        std::unordered_map<uint8_t, uint64_t> freqs;
        for (uint16_t i = 0; i < alphabet_size; ++i) {
            uint8_t character;
            uint64_t frequency;
            if (!in.read(reinterpret_cast<char*>(&character), sizeof(character)) ||
                !in.read(reinterpret_cast<char*>(&frequency), sizeof(frequency))) {
                std::cerr << "Error: Failed to read frequency table entry " << i << std::endl;
                return false;
            }
            freqs[character] = frequency;
        }

        // 2. Rebuild the tree
        HuffmanNode* root = buildTree(freqs);
        if (!root) {
            std::cerr << "Error: Failed to rebuild Huffman tree." << std::endl;
            out.close();
            return false;
        }

        // 3. Decode the packed bitstream
        BitReader reader(in);
        HuffmanNode* current = root;
        uint64_t decoded_count = 0;

        while (decoded_count < original_size) {
            int bit = reader.readBit();
            if (bit == -1) {
                std::cerr << "Error: Unexpected EOF in compressed bitstream. Decoded " 
                          << decoded_count << " of " << original_size << " symbols." << std::endl;
                delete root;
                out.close();
                return false;
            }

            if (bit == 0) {
                current = current->left;
            } else {
                current = current->right;
            }

            if (!current) {
                std::cerr << "Error: Reached null node in Huffman tree. Tree structure might be corrupted." << std::endl;
                delete root;
                out.close();
                return false;
            }

            // If a leaf is reached, write the character and restart from the root
            if (!current->left && !current->right) {
                out.put(static_cast<char>(current->character));
                decoded_count++;
                current = root;
            }
        }

        in.close();
        out.close();
        delete root;
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <option> <input_file> <output_file>\n"
                  << "Options:\n"
                  << "  -c : Compress input_file to output_file\n"
                  << "  -d : Decompress input_file to output_file\n";
        return 1;
    }

    std::string option = argv[1];
    std::string inputFile = argv[2];
    std::string outputFile = argv[3];

    HuffmanCompressor compressor;

    if (option == "-c") {
        std::cout << "Compressing \"" << inputFile << "\" to \"" << outputFile << "\"...\n";
        if (compressor.compress(inputFile, outputFile)) {
            std::cout << "Compression completed successfully.\n";
            return 0;
        } else {
            std::cerr << "Compression failed.\n";
            return 1;
        }
    } else if (option == "-d") {
        std::cout << "Decompressing \"" << inputFile << "\" to \"" << outputFile << "\"...\n";
        if (compressor.decompress(inputFile, outputFile)) {
            std::cout << "Decompression completed successfully.\n";
            return 0;
        } else {
            std::cerr << "Decompression failed.\n";
            return 1;
        }
    } else {
        std::cerr << "Error: Unknown option \"" << option << "\"\n";
        return 1;
    }
}

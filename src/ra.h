#pragma once

// Decoder for DiD "RA" compressed blocks (EF2000, Super EF2000 and v2.0).
//
// Reverse engineered from EF2000 v2.0's `_ef2000.exe`:
//   ra_decompress 0x108494, bit reader 0x108783/0x10879a,
//   tree builder 0x1086b9, checksum 0x10874c.
//
// Header (12 bytes, little endian):
//   0x00 "RA"   0x02 u16 version   0x04 u32 uncompressed size
//   0x08 u16 flags                 0x0a u16 checksum   0x0c stream
//
// The stream is a sequence of blocks. Each block starts with a raw u16 pair
// count, then three serialised binary Huffman trees (match, literal,
// distance), then that many (literal-run, match) pairs. A final bit says
// whether another block follows.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ef {

// Returns the decompressed payload, or std::nullopt if the block is invalid.
std::optional<std::vector<uint8_t>> ra_decode(std::span<const uint8_t> data);

// True if `data` starts with a plausible RA header.
bool ra_looks_valid(std::span<const uint8_t> data);

}  // namespace ef

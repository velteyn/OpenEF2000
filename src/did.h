#pragma once

// Reader for DiD's `DID.DAT` / `DID1ST.DAT` shape archives.
//
// Two directory variants ship with EF2000:
//   * `plain8` -- EF2000, Super EF2000, DID1ST.DAT (no encryption)
//   * `xorv2`  -- EF2000 v2.0 (XOR 0x2B85 header, 0x85 directory strings)
//
// Both end the 12-byte entry table exactly at EOF and sort entries by *signed*
// 32-bit hash. Entries are addressed by a hash of an eight-character canonical
// name, so the caller's path is rewritten before hashing:
//
//   1. lowercase, normalise '/' to '\', strip a leading '\';
//   2. replace a matching directory prefix with chr('0' + prefix index);
//   3. replace the extension with chr('A' + extension index).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ef {

struct DidEntry {
    std::uint32_t index = 0;
    std::uint32_t hash = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

class DidArchive {
public:
    // Throws std::runtime_error if the archive cannot be parsed.
    explicit DidArchive(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return path_; }
    const std::string& variant() const { return variant_; }
    const std::vector<std::string>& prefixes() const { return prefixes_; }
    const std::vector<std::string>& extensions() const { return extensions_; }
    const std::vector<DidEntry>& entries() const { return entries_; }

    // Rewrites a path to the canonical key the game hashes.
    std::string canonical(const std::string& name) const;

    // Hashes a canonical name the way this archive variant does.
    std::uint32_t hash_name(const std::string& canonical) const;

    std::optional<DidEntry> lookup(const std::string& name) const;

    // Convenience: lookup "<prefix><stem><extension>", e.g. "3\\", "efa", ".3".
    std::optional<DidEntry> resolve(const std::string& stem,
                                    const std::string& prefix = "3\\",
                                    const std::string& extension = ".3") const;

    // Raw (still compressed) bytes of an entry.
    std::vector<std::uint8_t> read_raw(const DidEntry& entry) const;

    // Decoded bytes; transparently decompresses RA blocks.
    std::optional<std::vector<std::uint8_t>> read(const DidEntry& entry) const;

    // Storage kind of an entry: "raw" or "ra".
    std::string storage(const DidEntry& entry) const;

private:
    void detect_and_parse();
    void parse_plain8(std::size_t offset);
    void parse_xorv2();

    std::filesystem::path path_;
    std::vector<std::uint8_t> data_;
    std::string variant_;
    std::vector<std::string> prefixes_;
    std::vector<std::string> extensions_;
    std::vector<DidEntry> entries_;
};

// The alternate hash used by every variant except v2.0's primary lookup.
std::uint32_t name_hash_alt(const std::string& name);

// The hash v2.0 uses first (mod 0xffffb).
std::uint32_t name_hash_v2(const std::string& name);

}  // namespace ef

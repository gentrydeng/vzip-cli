// SPDX-License-Identifier: MIT
#pragma once
#include "VZip.h"
#include <cstring>
#include <vector>
#include <stdexcept>
struct MemoryReader : vzip::Reader {
    std::vector<uint8_t> bytes;
    explicit MemoryReader(std::vector<uint8_t> value) : bytes(std::move(value)) {}
    uint64_t Size() const override { return bytes.size(); }
    void Read(uint64_t offset, uint8_t* data, size_t count) override {
        if (offset > bytes.size() || count > bytes.size() - offset) throw std::runtime_error("read bounds");
        std::memcpy(data, bytes.data() + size_t(offset), count);
    }
};
struct MemorySink : vzip::Sink {
    std::vector<uint8_t> bytes;
    void Write(const uint8_t* data, size_t count) override { bytes.insert(bytes.end(), data, data + count); }
};
inline void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
// Independent fixture: Python stdlib raw LZMA with lc=3/lp=0/pb=2,
// 8 MiB dictionary and EOS marker. Decompressed payload: "Valve VZip test\n".
inline std::vector<uint8_t> FixtureVZa() {
    return {0x56,0x5a,0x61,0,0,0,0,0x5d,0,0,0x80,0,0,0x2b,0x18,0x49,
        0x87,0x99,0x89,0xc5,0xfc,0x90,0xb8,0x6f,0x50,0xfc,0x8b,0x75,0xff,
        0xff,0x76,0x70,0xb5,0x3f,0xff,0xfc,0x32,0x70,0,0xfd,0xcc,0x6f,0x8c,
        0x10,0,0,0,0x7a,0x76};
}

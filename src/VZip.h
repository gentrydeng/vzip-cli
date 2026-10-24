// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace vzip {
enum class Format { VZa, VSZa };
enum class ErrorCode { NotArchive, Unsupported, Header, Data, Crc, Limit, Io };
class Error : public std::runtime_error {
public:
    ErrorCode code;
    Error(ErrorCode value, const char* message) : std::runtime_error(message), code(value) {}
};
class Reader {
public:
    virtual ~Reader() = default;
    virtual uint64_t Size() const = 0;
    // Read exactly count bytes, or throw. Offsets are relative to this stream.
    virtual void Read(uint64_t offset, uint8_t* data, size_t count) = 0;
};
class Sink {
public:
    virtual ~Sink() = default;
    virtual void Write(const uint8_t* data, size_t count) = 0;
};
struct Frame {
    Format format;
    uint64_t payload_offset;
    uint64_t packed_size;
    uint32_t unpacked_size;
    uint32_t crc;
    uint32_t header_value; // VZa timestamp/CRC: interpretation depends on producer.
    uint32_t reserved;     // VSZa footer field: meaning not established.
    uint8_t properties[5]{};
};
struct Limits {
    uint64_t output = UINT32_MAX;
    uint32_t dictionary = 128u << 20;
};
using Progress = std::function<void(uint64_t input, uint64_t output)>;
Frame Inspect(Reader& reader, const Limits& limits = {});
void Decode(Reader& reader, const Frame& frame, Sink& sink,
            const Progress& progress = {}, const Limits& limits = {});
const char* FormatName(Format format);
}

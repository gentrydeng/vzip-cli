// SPDX-License-Identifier: MIT
#include "VZip.h"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
extern "C" {
#include "7zCrc.h"
#include "Alloc.h"
#include "LzmaDec.h"
#include "zstd.h"
}

namespace vzip {
namespace {
constexpr size_t kBufferSize = 64u << 10;
uint32_t Read32(const uint8_t* data) {
    return uint32_t(data[0]) | uint32_t(data[1]) << 8 |
           uint32_t(data[2]) << 16 | uint32_t(data[3]) << 24;
}
class CheckedOutput {
    Sink& sink_;
    const Frame& frame_;
    uint32_t crc_ = CRC_INIT_VAL;
public:
    uint64_t size = 0;
    CheckedOutput(Sink& sink, const Frame& frame) : sink_(sink), frame_(frame) {
        static std::once_flag once;
        std::call_once(once, CrcGenerateTable);
    }
    void Write(const uint8_t* data, size_t count) {
        if (count > uint64_t(frame_.unpacked_size) - size)
            throw Error(ErrorCode::Data, "Decoded stream exceeds declared size");
        crc_ = CrcUpdate(crc_, data, count);
        sink_.Write(data, count);
        size += count;
    }
    void Finish() const {
        if (size != frame_.unpacked_size)
            throw Error(ErrorCode::Data, "Decoded stream length mismatch");
        if (CRC_GET_DIGEST(crc_) != frame_.crc)
            throw Error(ErrorCode::Crc, "Decoded stream CRC32 mismatch");
    }
};
class InputBuffer {
    Reader& reader_;
    const Frame& frame_;
    std::array<uint8_t, kBufferSize> data_{};
    uint64_t loaded_ = 0;
    size_t position_ = 0;
    size_t available_ = 0;
public:
    InputBuffer(Reader& reader, const Frame& frame) : reader_(reader), frame_(frame) {}
    bool Fill() {
        if (position_ < available_) return true;
        available_ = size_t(std::min<uint64_t>(data_.size(), frame_.packed_size - loaded_));
        position_ = 0;
        if (!available_) return false;
        reader_.Read(frame_.payload_offset + loaded_, data_.data(), available_);
        loaded_ += available_;
        return true;
    }
    const uint8_t* Data() const { return data_.data() + position_; }
    size_t Available() const { return available_ - position_; }
    void Consume(size_t count) { position_ += count; }
    uint64_t Consumed() const { return loaded_ - Available(); }
};
void DecodeLzma(Reader& reader, const Frame& frame, CheckedOutput& output,
                const Progress& progress) {
    struct Decoder {
        CLzmaDec state;
        Decoder() { LzmaDec_Construct(&state); }
        ~Decoder() { LzmaDec_Free(&state, &g_Alloc); }
    } decoder;
    SRes result = LzmaDec_Allocate(&decoder.state, frame.properties, 5, &g_Alloc);
    if (result == SZ_ERROR_MEM) throw std::bad_alloc();
    if (result != SZ_OK) throw Error(ErrorCode::Header, "Unsupported LZMA properties");
    LzmaDec_Init(&decoder.state);
    InputBuffer input(reader, frame);
    std::array<uint8_t, kBufferSize> buffer{};
    for (;;) {
        input.Fill();
        SizeT used = input.Available();
        const uint64_t remaining = frame.unpacked_size - output.size;
        SizeT produced = size_t(std::min<uint64_t>(buffer.size(), remaining));
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        result = LzmaDec_DecodeToBuf(&decoder.state, buffer.data(), &produced,
            input.Data(), &used,
            remaining <= buffer.size() ? LZMA_FINISH_END : LZMA_FINISH_ANY, &status);
        input.Consume(used);
        if (result != SZ_OK) throw Error(ErrorCode::Data, "Invalid LZMA stream");
        output.Write(buffer.data(), produced);
        if (progress) progress(input.Consumed(), output.size);
        const bool finished = status == LZMA_STATUS_FINISHED_WITH_MARK ||
            (status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK &&
             output.size == frame.unpacked_size && input.Consumed() == frame.packed_size);
        if (finished) {
            if (input.Consumed() != frame.packed_size)
                throw Error(ErrorCode::Data, "Trailing data after LZMA stream");
            break;
        }
        if (!used && !produced)
            throw Error(ErrorCode::Data, "Truncated or oversized LZMA stream");
        if (!input.Available() && input.Consumed() == frame.packed_size &&
            status == LZMA_STATUS_NEEDS_MORE_INPUT)
            throw Error(ErrorCode::Data, "Truncated LZMA stream");
    }
}
void DecodeZstd(Reader& reader, const Frame& frame, CheckedOutput& output,
                const Progress& progress, const Limits& limits) {
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    if (!context) throw std::bad_alloc();
    unsigned window_log = 0;
    for (uint32_t value = limits.dictionary; value > 1; value >>= 1) ++window_log;
    const size_t setting = ZSTD_DCtx_setParameter(context.get(), ZSTD_d_windowLogMax, int(window_log));
    if (ZSTD_isError(setting)) throw Error(ErrorCode::Limit, "Invalid Zstandard window limit");
    InputBuffer input(reader, frame);
    std::array<uint8_t, kBufferSize> buffer{};
    for (;;) {
        input.Fill();
        ZSTD_inBuffer source{input.Data(), input.Available(), 0};
        ZSTD_outBuffer destination{buffer.data(), buffer.size(), 0};
        const size_t result = ZSTD_decompressStream(context.get(), &destination, &source);
        if (ZSTD_isError(result)) throw Error(ErrorCode::Data, "Invalid or excessive-window Zstandard stream");
        input.Consume(source.pos);
        output.Write(buffer.data(), destination.pos);
        if (progress) progress(input.Consumed(), output.size);
        if (result == 0) {
            if (input.Consumed() != frame.packed_size)
                throw Error(ErrorCode::Data, "Trailing data after Zstandard frame");
            break;
        }
        if (!source.pos && !destination.pos)
            throw Error(ErrorCode::Data, "Truncated Zstandard stream");
    }
}
}
const char* FormatName(Format format) { return format == Format::VZa ? "VZa" : "VSZa"; }
Frame Inspect(Reader& reader, const Limits& limits) {
    const uint64_t size = reader.Size();
    if (size < 3) throw Error(ErrorCode::NotArchive, "Not a Valve compressed stream");
    uint8_t header[12]{};
    reader.Read(0, header, size_t(std::min<uint64_t>(size, sizeof(header))));
    Frame frame{};
    if (header[0] == 'V' && header[1] == 'Z') {
        if (header[2] == 'd') throw Error(ErrorCode::Unsupported, "VZd support is deferred; original block required");
        if (header[2] != 'a') throw Error(ErrorCode::Unsupported, "Unsupported VZ version");
        if (size < 22) throw Error(ErrorCode::Header, "Truncated VZa frame");
        uint8_t footer[10];
        reader.Read(size - sizeof(footer), footer, sizeof(footer));
        if (footer[8] != 'z' || footer[9] != 'v')
            throw Error(ErrorCode::Header, "Invalid VZa footer");
        frame.format = Format::VZa;
        frame.payload_offset = 12;
        frame.packed_size = size - 22;
        frame.header_value = Read32(header + 3);
        frame.crc = Read32(footer);
        frame.unpacked_size = Read32(footer + 4);
        std::copy(header + 7, header + 12, frame.properties);
        if (frame.properties[0] >= 225)
            throw Error(ErrorCode::Header, "Invalid LZMA properties");
        if (Read32(frame.properties + 1) > limits.dictionary)
            throw Error(ErrorCode::Limit, "LZMA dictionary exceeds configured limit");
    } else if (size >= 4 && header[0] == 'V' && header[1] == 'S' &&
               header[2] == 'Z' && header[3] == 'a') {
        if (size < 23) throw Error(ErrorCode::Header, "Truncated VSZa frame");
        uint8_t footer[15];
        reader.Read(size - sizeof(footer), footer, sizeof(footer));
        if (footer[12] != 'z' || footer[13] != 's' || footer[14] != 'v')
            throw Error(ErrorCode::Header, "Invalid VSZa footer");
        frame.format = Format::VSZa;
        frame.payload_offset = 8;
        frame.packed_size = size - 23;
        frame.header_value = Read32(header + 4);
        frame.crc = Read32(footer);
        frame.unpacked_size = Read32(footer + 4);
        frame.reserved = Read32(footer + 8);
        if (frame.header_value != frame.crc)
            throw Error(ErrorCode::Header, "VSZa header/footer CRC values differ");
    } else {
        throw Error(ErrorCode::NotArchive, "Not a supported Valve compressed stream");
    }
    if (!frame.packed_size) throw Error(ErrorCode::Header, "Missing compressed stream");
    if (frame.unpacked_size > limits.output)
        throw Error(ErrorCode::Limit, "Decoded size exceeds configured limit");
    return frame;
}
void Decode(Reader& reader, const Frame& frame, Sink& sink,
            const Progress& progress, const Limits& limits) {
    if (frame.unpacked_size > limits.output)
        throw Error(ErrorCode::Limit, "Decoded size exceeds configured limit");
    if (frame.format == Format::VZa && Read32(frame.properties + 1) > limits.dictionary)
        throw Error(ErrorCode::Limit, "LZMA dictionary exceeds configured limit");
    CheckedOutput output(sink, frame);
    if (progress) progress(0, 0);
    if (frame.format == Format::VZa) DecodeLzma(reader, frame, output, progress);
    else DecodeZstd(reader, frame, output, progress, limits);
    output.Finish();
}
}

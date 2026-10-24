// SPDX-License-Identifier: MIT
#include "TestSupport.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <string>
extern "C" {
#include "Alloc.h"
#include "7zCrc.h"
#include "LzmaEnc.h"
#include "zstd.h"
}
namespace {
void Append32(std::vector<uint8_t>& data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.push_back(uint8_t(value >> (8 * i)));
}
std::vector<uint8_t> Pack(const std::vector<uint8_t>& data, bool zstd, bool marker) {
    std::vector<uint8_t> packed(data.size() * 2 + 1024);
    const uint32_t crc = CrcCalc(data.data(), data.size());
    std::vector<uint8_t> frame;
    if (zstd) {
        const size_t size = ZSTD_compress(packed.data(), packed.size(), data.data(), data.size(), 3);
        Require(!ZSTD_isError(size), "zstd fixture encoding");
        packed.resize(size);
        frame = {'V','S','Z','a'};
        Append32(frame, crc);
    } else {
        CLzmaEncProps properties;
        LzmaEncProps_Init(&properties);
        properties.dictSize = 1 << 20;
        properties.numThreads = 1;
        uint8_t encoded[5];
        SizeT properties_size = 5, size = packed.size();
        const auto result = LzmaEncode(packed.data(), &size, data.data(), data.size(), &properties,
            encoded, &properties_size, marker ? 1 : 0, nullptr, &g_Alloc, &g_Alloc);
        Require(result == SZ_OK, "lzma fixture encoding");
        packed.resize(size);
        frame = {'V','Z','a',0,0,0,0};
        frame.insert(frame.end(), encoded, encoded + 5);
    }
    frame.insert(frame.end(), packed.begin(), packed.end());
    Append32(frame, crc);
    Append32(frame, uint32_t(data.size()));
    if (zstd) { Append32(frame, 0); frame.insert(frame.end(), {'z','s','v'}); }
    else frame.insert(frame.end(), {'z','v'});
    return frame;
}
void Verify(std::vector<uint8_t> bytes, const std::vector<uint8_t>& expected) {
    MemoryReader reader(std::move(bytes));
    const auto frame = vzip::Inspect(reader);
    MemorySink output;
    uint64_t previous = 0;
    vzip::Decode(reader, frame, output, [&](uint64_t, uint64_t completed) {
        Require(completed >= previous, "nonmonotonic progress");
        previous = completed;
    });
    Require(output.bytes == expected, "decoded bytes differ");
    Require(previous == expected.size(), "final progress differs");
}
void Reject(std::vector<uint8_t> bytes) {
    MemoryReader reader(std::move(bytes));
    MemorySink output;
    bool rejected = false;
    try { const auto frame = vzip::Inspect(reader); vzip::Decode(reader, frame, output); }
    catch (const vzip::Error&) { rejected = true; }
    Require(rejected, "corrupt fixture was accepted");
}
void Save(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    Require(bool(file), "cannot save fixture");
}
}
int main(int argc, char** argv) {
    CrcGenerateTable();
    unsigned passed = 0, failed = 0;
    const auto test = [&](const std::string& name, const std::function<void()>& body) {
        try { body(); ++passed; }
        catch (const std::exception& error) { ++failed; std::cerr << name << ": " << error.what() << '\n'; }
    };
    const std::vector<uint8_t> plain{'V','a','l','v','e',' ','V','Z','i','p',' ','t','e','s','t','\n'};
    test("independent Python LZMA vector", [&] { Verify(FixtureVZa(), plain); });
    for (const bool zstd : {false, true}) {
        const std::string prefix = zstd ? "VSZa " : "VZa ";
        for (const size_t size : {size_t(0),size_t(1),size_t(65536),size_t(65537),size_t(2 << 20)}) {
            std::vector<uint8_t> payload(size);
            uint32_t random = 0x12345678;
            for (auto& byte : payload) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; byte = uint8_t(random); }
            test(prefix + std::to_string(size) + " EOS", [&] { Verify(Pack(payload, zstd, true), payload); });
            if (!zstd) test(prefix + std::to_string(size) + " no EOS", [&] { Verify(Pack(payload, false, false), payload); });
        }
        std::vector<uint8_t> repetitive(3 << 20, 'a');
        test(prefix + "large repetitive output", [&] { Verify(Pack(repetitive, zstd, false), repetitive); });
        const auto frame = Pack(plain, zstd, true);
        for (size_t cut = 0; cut < frame.size(); ++cut)
            test(prefix + "truncation " + std::to_string(cut), [&] { Reject({frame.begin(), frame.begin() + cut}); });
        test(prefix + "wrong CRC", [&] { auto bad = frame; bad[bad.size() - (zstd ? 15 : 10)] ^= 1; Reject(bad); });
        test(prefix + "wrong output length", [&] { auto bad = frame; bad[bad.size() - (zstd ? 11 : 6)] ^= 1; Reject(bad); });
        test(prefix + "payload damage", [&] { auto bad = frame; bad[zstd ? 8 : 12] ^= 0xff; Reject(bad); });
        test(prefix + "trailing compressed data", [&] { auto bad = frame; bad.insert(bad.end() - (zstd ? 15 : 10), 0); Reject(bad); });
        test(prefix + "resource policy", [&] {
            MemoryReader input(frame);
            vzip::Limits limits; limits.output = 1;
            bool rejected = false;
            try { vzip::Inspect(input, limits); } catch (const vzip::Error&) { rejected = true; }
            Require(rejected, "output policy ignored");
        });
        test(prefix + "cancellation propagates", [&] {
            MemoryReader input(frame); MemorySink output;
            bool cancelled = false;
            try { vzip::Decode(input, vzip::Inspect(input), output, [](uint64_t, uint64_t) { throw 42; }); }
            catch (int) { cancelled = true; }
            Require(cancelled, "cancellation swallowed");
        });
        if (argc == 3 && std::string(argv[1]) == "--fixtures") {
            const std::filesystem::path directory(argv[2]);
            std::filesystem::create_directories(directory);
            Save(directory / (zstd ? "sample.vsz" : "sample.vz"), frame);
            auto bad = frame; bad[bad.size() - (zstd ? 15 : 10)] ^= 1;
            if (zstd) bad[4] ^= 1;
            Save(directory / (zstd ? "bad-crc.vsz" : "bad-crc.vz"), bad);
            Save(directory / "expected.bin", plain);
        }
    }
    test("dictionary limit", [&] { auto bad = FixtureVZa(); bad[11] = 0x7f; Reject(bad); });
    test("invalid LZMA properties", [&] { auto bad = FixtureVZa(); bad[7] = 225; Reject(bad); });
    test("deferred VZd", [&] { auto bad = FixtureVZa(); bad[2] = 'd'; Reject(bad); });
    test("VSZa concatenated frames rejected", [&] {
        auto a = Pack(plain, true, true), b = a;
        a.insert(a.end() - 15, b.begin() + 8, b.end() - 15);
        Reject(a);
    });
    std::cout << "core passed=" << passed << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}

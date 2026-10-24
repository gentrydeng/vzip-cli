// SPDX-License-Identifier: MIT
#include "VZip.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace {
class FileReader final : public vzip::Reader {
    std::ifstream file_;
    uint64_t size_;
public:
    explicit FileReader(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        if (!file_) throw vzip::Error(vzip::ErrorCode::Io, "Cannot open input file");
        file_.seekg(0, std::ios::end);
        const auto size = file_.tellg();
        if (size < 0) throw vzip::Error(vzip::ErrorCode::Io, "Cannot determine input size");
        size_ = uint64_t(size);
    }
    uint64_t Size() const override { return size_; }
    void Read(uint64_t offset, uint8_t* data, size_t count) override {
        if (offset > size_ || count > size_ - offset)
            throw vzip::Error(vzip::ErrorCode::Io, "Input offset out of range");
        file_.clear();
        file_.seekg(std::streamoff(offset));
        file_.read(reinterpret_cast<char*>(data), std::streamsize(count));
        if (!file_) throw vzip::Error(vzip::ErrorCode::Io, "Input read failed");
    }
};
class NullSink final : public vzip::Sink {
    void Write(const uint8_t*, size_t) override {}
};
class AtomicFile final : public vzip::Sink {
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::filesystem::path temporary_;
    std::filesystem::path destination_;
public:
    explicit AtomicFile(const std::filesystem::path& destination) : destination_(std::filesystem::absolute(destination)) {
        if (GetFileAttributesW(destination_.c_str()) != INVALID_FILE_ATTRIBUTES)
            throw vzip::Error(vzip::ErrorCode::Io, "Output already exists; refusing overwrite");
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            temporary_ = destination_;
            temporary_ += L".vzip-tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
            file_ = CreateFileW(temporary_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (file_ != INVALID_HANDLE_VALUE) return;
            if (GetLastError() != ERROR_FILE_EXISTS) break;
        }
        temporary_.clear();
        throw vzip::Error(vzip::ErrorCode::Io, "Cannot create temporary output");
    }
    ~AtomicFile() {
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        if (!temporary_.empty()) DeleteFileW(temporary_.c_str());
    }
    void Write(const uint8_t* data, size_t count) override {
        DWORD written = 0;
        if (!WriteFile(file_, data, DWORD(count), &written, nullptr) || written != count)
            throw vzip::Error(vzip::ErrorCode::Io, "Output write failed");
    }
    void Commit() {
        if (!FlushFileBuffers(file_)) throw vzip::Error(vzip::ErrorCode::Io, "Output flush failed");
        if (!CloseHandle(file_)) { file_ = INVALID_HANDLE_VALUE; throw vzip::Error(vzip::ErrorCode::Io, "Output close failed"); }
        file_ = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_WRITE_THROUGH))
            throw vzip::Error(vzip::ErrorCode::Io, "Cannot commit output; an existing output is never replaced");
        temporary_.clear();
    }
};
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--help") {
        std::cout << "VZip 0.1.0\nUsage / 用法:\n"
                  << "  vzip inspect INPUT\n  vzip test INPUT\n  vzip extract INPUT OUTPUT\n"
                  << "VZa / VSZa only. Existing output is never overwritten.\n"
                  << "仅支持 VZa / VSZa，不覆盖已有输出。\n";
        return 0;
    }
    const std::wstring command = argc > 1 ? argv[1] : L"";
    if (!((argc == 3 && (command == L"inspect" || command == L"test")) ||
          (argc == 4 && command == L"extract"))) {
        std::cerr << "Invalid arguments / 参数错误。Use vzip --help.\n";
        return 2;
    }
    try {
        FileReader reader(argv[2]);
        const auto frame = vzip::Inspect(reader);
        if (command == L"inspect") {
            std::cout << "format=" << vzip::FormatName(frame.format)
                      << "\npacked_size=" << frame.packed_size
                      << "\nunpacked_size=" << frame.unpacked_size
                      << "\ncrc32=" << std::hex << frame.crc << std::dec
                      << "\nintegrity_checked=false\n";
            return 0;
        }
        if (command == L"test") {
            NullSink sink;
            vzip::Decode(reader, frame, sink);
        } else {
            AtomicFile output(argv[3]);
            vzip::Decode(reader, frame, output);
            output.Commit();
        }
        std::cout << "OK / 成功: " << vzip::FormatName(frame.format) << ", " << frame.unpacked_size << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error / 错误: " << error.what() << '\n';
        return 1;
    }
}

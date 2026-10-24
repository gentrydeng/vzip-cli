// SPDX-License-Identifier: MIT
#include "Common/MyInitGuid.h"
#include "7zip/Archive/IArchive.h"
#include "TestSupport.h"
#include <algorithm>
#include <iostream>
#include <string>

namespace {
struct Input final : IInStream {
    ULONG refs = 1;
    std::vector<uint8_t> bytes = FixtureVZa();
    uint64_t position = 0;
    bool fail = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_ISequentialInStream && iid != IID_IInStream) return E_NOINTERFACE;
        *object = static_cast<IInStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs; }
    HRESULT STDMETHODCALLTYPE Read(void* data, UInt32 size, UInt32* done) noexcept override {
        *done = 0;
        if (fail) return E_ACCESSDENIED;
        const size_t count = std::min<size_t>({size, 7, bytes.size() - size_t(position)});
        std::memcpy(data, bytes.data() + size_t(position), count);
        position += count; *done = UInt32(count); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 origin, UInt64* result) noexcept override {
        Int64 base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? Int64(position) : Int64(bytes.size());
        if (offset < -base || uint64_t(base + offset) > bytes.size()) return E_INVALIDARG;
        position = uint64_t(base + offset);
        if (result) *result = position;
        return S_OK;
    }
};
struct Output final : ISequentialOutStream {
    ULONG refs = 1;
    std::vector<uint8_t> bytes;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_ISequentialOutStream) return E_NOINTERFACE;
        *object = static_cast<ISequentialOutStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs; }
    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 count, UInt32* done) noexcept override {
        *done = std::min<UInt32>(count, 3);
        const auto first = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), first, first + *done);
        return S_OK;
    }
};
struct Callback final : IArchiveExtractCallback {
    Output output;
    Int32 result = -1;
    bool abort = false;
    bool skip = false;
    ULONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IProgress && iid != IID_IArchiveExtractCallback) return E_NOINTERFACE;
        *object = static_cast<IArchiveExtractCallback*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs; }
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*) noexcept override { return abort ? E_ABORT : S_OK; }
    HRESULT STDMETHODCALLTYPE GetStream(UInt32, ISequentialOutStream** stream, Int32 mode) noexcept override {
        *stream = nullptr;
        if (!skip && mode == NArchive::NExtract::NAskMode::kExtract) { *stream = &output; output.AddRef(); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 value) noexcept override { result = value; return S_OK; }
};
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { std::cerr << "LoadLibrary error=" << GetLastError() << '\n'; return 1; }
    IInArchive* archive = nullptr;
    unsigned passed = 0;
    try {
        using Count = HRESULT (WINAPI*)(UInt32*);
        using Property = HRESULT (WINAPI*)(UInt32, PROPID, PROPVARIANT*);
        using Create = HRESULT (WINAPI*)(const GUID*, const GUID*, void**);
        const auto count = reinterpret_cast<Count>(GetProcAddress(module, "GetNumberOfFormats"));
        const auto property = reinterpret_cast<Property>(GetProcAddress(module, "GetHandlerProperty2"));
        const auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateObject"));
        Require(count && property && create, "missing exports"); ++passed;
        UInt32 formats = 0;
        Require(count(&formats) == S_OK && formats == 2, "format count"); ++passed;
        for (UInt32 index = 0; index < formats; ++index) {
            PROPVARIANT value{};
            Require(property(index, NArchive::NHandlerPropID::kClassID, &value) == S_OK &&
                value.vt == VT_BSTR && SysStringByteLen(value.bstrVal) == sizeof(GUID), "class property");
            GUID class_id; std::memcpy(&class_id, value.bstrVal, sizeof(class_id)); PropVariantClear(&value);
            void* object = nullptr;
            Require(create(&class_id, &IID_IInArchive, &object) == S_OK, "create object");
            static_cast<IInArchive*>(object)->Release();
            Require(property(index, NArchive::NHandlerPropID::kUpdate, &value) == S_OK && value.boolVal == VARIANT_FALSE,
                "read-only declaration"); PropVariantClear(&value); ++passed;
            if (index == 0) { Require(create(&class_id, &IID_IInArchive, reinterpret_cast<void**>(&archive)) == S_OK, "create VZa"); }
        }
        Input input;
        Require(archive->Open(&input, nullptr, nullptr) == S_OK, "Open"); ++passed;
        UInt32 items = 0; archive->GetNumberOfItems(&items); Require(items == 1, "item count"); ++passed;
        Callback extraction;
        Require(archive->Extract(nullptr, UINT32_MAX, 0, &extraction) == S_OK && extraction.result == 0, "extraction");
        Require(std::string(extraction.output.bytes.begin(), extraction.output.bytes.end()) == "Valve VZip test\n", "partial IO bytes"); ++passed;
        Require(extraction.output.refs == 1, "output stream released"); ++passed;
        Callback testing;
        Require(archive->Extract(nullptr, UINT32_MAX, 1, &testing) == S_OK && testing.result == 0 && testing.output.bytes.empty(), "test mode"); ++passed;
        Callback cancellation; cancellation.abort = true;
        Require(archive->Extract(nullptr, UINT32_MAX, 1, &cancellation) == E_ABORT, "cancellation"); ++passed;
        Callback skipped; skipped.skip = true;
        Require(archive->Extract(nullptr, UINT32_MAX, 0, &skipped) == S_OK && skipped.result == -1, "skip"); ++passed;
        UInt32 invalid = 1;
        Require(archive->Extract(&invalid, 1, 0, &testing) == E_INVALIDARG, "invalid selection"); ++passed;
        input.bytes[input.bytes.size() - 10] ^= 1;
        Callback corrupt;
        Require(archive->Extract(nullptr, UINT32_MAX, 1, &corrupt) == S_OK &&
            corrupt.result == NArchive::NExtract::NOperationResult::kCRCError, "CRC result"); ++passed;
        input.fail = true;
        Callback fault;
        Require(archive->Extract(nullptr, UINT32_MAX, 1, &fault) == E_ACCESSDENIED, "input IO error propagation"); ++passed;
        archive->Close(); Require(input.refs == 1, "input stream released"); ++passed;
        archive->Release(); archive = nullptr;
        std::cout << "plugin passed=" << passed << " failed=0\n";
        FreeLibrary(module); return 0;
    } catch (const std::exception& error) {
        if (archive) archive->Release();
        std::cerr << "plugin passed=" << passed << " failed=1: " << error.what() << '\n';
        FreeLibrary(module); return 1;
    }
}

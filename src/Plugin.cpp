// SPDX-License-Identifier: MIT
#include "Common/MyInitGuid.h"
#include "7zip/Archive/IArchive.h"
#include "VZip.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwctype>
#include <new>
#include <string>

namespace {
const GUID kClasses[] = {
    {0x973af634, 0x3c9e, 0x44b2, {0x98, 0xae, 0x39, 0xd6, 0xc8, 0x6b, 0x17, 0x01}},
    {0x973af634, 0x3c9e, 0x44b2, {0x98, 0xae, 0x39, 0xd6, 0xc8, 0x6b, 0x17, 0x02}}
};
struct HResultError { HRESULT value; };
void Check(HRESULT result) { if (result != S_OK) throw HResultError{FAILED(result) ? result : E_FAIL}; }
template<class Function> HRESULT Boundary(Function function) noexcept {
    try { return function(); }
    catch (const HResultError& error) { return error.value; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
HRESULT Text(PROPVARIANT* value, const wchar_t* text) {
    value->bstrVal = SysAllocString(text);
    if (!value->bstrVal) return E_OUTOFMEMORY;
    value->vt = VT_BSTR;
    return S_OK;
}
void Number(PROPVARIANT* value, uint64_t number) {
    value->vt = VT_UI8;
    value->uhVal.QuadPart = number;
}
template<class Interface> class ComOwner {
    Interface* pointer_ = nullptr;
public:
    ~ComOwner() { if (pointer_) pointer_->Release(); }
    Interface** Address() { return &pointer_; }
    Interface* Get() const { return pointer_; }
};
class StreamReader final : public vzip::Reader {
    IInStream* stream_;
    uint64_t size_;
public:
    explicit StreamReader(IInStream* stream) : stream_(stream) {
        UInt64 size = 0;
        Check(stream_->Seek(0, STREAM_SEEK_END, &size));
        size_ = size;
    }
    uint64_t Size() const override { return size_; }
    void Read(uint64_t offset, uint8_t* data, size_t count) override {
        if (offset > size_ || count > size_ - offset || offset > INT64_MAX)
            throw vzip::Error(vzip::ErrorCode::Io, "Input offset is out of range");
        Check(stream_->Seek(Int64(offset), STREAM_SEEK_SET, nullptr));
        while (count) {
            const UInt32 request = UInt32(std::min<size_t>(count, UINT32_MAX));
            UInt32 done = 0;
            Check(stream_->Read(data, request, &done));
            if (!done || done > request) throw HResultError{E_FAIL};
            data += done;
            count -= done;
        }
    }
};
class StreamSink final : public vzip::Sink {
    ISequentialOutStream* stream_;
public:
    explicit StreamSink(ISequentialOutStream* stream) : stream_(stream) {}
    void Write(const uint8_t* data, size_t count) override {
        if (!stream_) return; // Test mode still decodes and validates every byte.
        while (count) {
            const UInt32 request = UInt32(std::min<size_t>(count, UINT32_MAX));
            UInt32 done = 0;
            Check(stream_->Write(data, request, &done));
            if (!done || done > request) throw HResultError{E_FAIL};
            data += done;
            count -= done;
        }
    }
};
struct Property { PROPID id; VARTYPE type; };
const Property kProperties[] = {
    {kpidPath, VT_BSTR}, {kpidSize, VT_UI8}, {kpidPackSize, VT_UI8},
    {kpidCRC, VT_UI4}, {kpidMethod, VT_BSTR}
};
const Property kArchiveProperties[] = {{kpidPhySize, VT_UI8}, {kpidMethod, VT_BSTR}};
HRESULT PropertyInfo(const Property* properties, size_t count, UInt32 index,
                     BSTR* name, PROPID* id, VARTYPE* type) {
    if (!name || !id || !type) return E_POINTER;
    *name = nullptr;
    if (index >= count) return E_INVALIDARG;
    *id = properties[index].id;
    *type = properties[index].type;
    return S_OK;
}
class Handler final : public IInArchive {
    std::atomic<ULONG> references_{1};
    vzip::Format expected_;
    IInStream* stream_ = nullptr;
    vzip::Frame frame_{};
    std::wstring name_;
public:
    explicit Handler(vzip::Format format) : expected_(format) {}
    ~Handler() { Close(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IInArchive) return E_NOINTERFACE;
        *object = static_cast<IInArchive*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Open(IInStream* stream, const UInt64*, IArchiveOpenCallback* callback) noexcept override {
        return Boundary([&]() -> HRESULT {
            Close();
            if (!stream) return E_POINTER;
            StreamReader reader(stream);
            try { frame_ = vzip::Inspect(reader); }
            catch (const vzip::Error&) { return S_FALSE; }
            if (frame_.format != expected_) return S_FALSE;
            name_ = L"payload";
            if (callback) {
                ComOwner<IArchiveOpenVolumeCallback> volume;
                if (callback->QueryInterface(IID_IArchiveOpenVolumeCallback,
                    reinterpret_cast<void**>(volume.Address())) == S_OK) {
                    PROPVARIANT name{};
                    const HRESULT result = volume.Get()->GetProperty(kpidName, &name);
                    if (result == S_OK && name.vt == VT_BSTR && name.bstrVal) {
                        std::wstring candidate(name.bstrVal, SysStringLen(name.bstrVal));
                        const auto separator = candidate.find_last_of(L"/\\:");
                        if (separator != std::wstring::npos) candidate.erase(0, separator + 1);
                        std::wstring lower = candidate;
                        std::transform(lower.begin(), lower.end(), lower.begin(),
                            [](wchar_t value) { return wchar_t(towlower(value)); });
                        size_t suffix = lower.rfind(L".vz");
                        if (suffix == std::wstring::npos) suffix = lower.rfind(L".vsz");
                        if (suffix != std::wstring::npos) candidate.resize(suffix);
                        // Expose one plain filename; never use an archive-provided path.
                        if (!candidate.empty() && candidate != L"." && candidate != L"..")
                            name_ = candidate;
                    }
                    PropVariantClear(&name);
                }
                const UInt64 files = 1, bytes = reader.Size();
                Check(callback->SetTotal(&files, &bytes));
                Check(callback->SetCompleted(&files, &bytes));
            }
            stream_ = stream;
            stream_->AddRef();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Close() noexcept override {
        if (stream_) stream_->Release();
        stream_ = nullptr;
        name_.clear();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberOfItems(UInt32* count) noexcept override {
        if (!count) return E_POINTER;
        *count = stream_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID id, PROPVARIANT* value) noexcept override {
        if (!value) return E_POINTER;
        PropVariantInit(value);
        if (!stream_ || index != 0) return E_INVALIDARG;
        switch (id) {
        case kpidPath: return Text(value, name_.c_str());
        case kpidSize: Number(value, frame_.unpacked_size); break;
        case kpidPackSize: Number(value, frame_.packed_size); break;
        case kpidCRC: value->vt = VT_UI4; value->ulVal = frame_.crc; break;
        case kpidMethod: return Text(value, expected_ == vzip::Format::VZa ? L"LZMA" : L"ZSTD");
        default: break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetArchiveProperty(PROPID id, PROPVARIANT* value) noexcept override {
        return Boundary([&]() -> HRESULT {
            if (!value) return E_POINTER;
            PropVariantInit(value);
            if (!stream_) return E_FAIL;
            if (id == kpidPhySize) { StreamReader reader(stream_); Number(value, reader.Size()); }
            if (id == kpidMethod) return Text(value, expected_ == vzip::Format::VZa ? L"LZMA" : L"ZSTD");
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Extract(const UInt32* indices, UInt32 count, Int32 test,
                                      IArchiveExtractCallback* callback) noexcept override {
        return Boundary([&]() -> HRESULT {
            if (!callback) return E_POINTER;
            if (!stream_) return E_FAIL;
            if (count == 0) return S_OK;
            if (count != UINT32_MAX && (count != 1 || !indices || indices[0] != 0)) return E_INVALIDARG;
            Check(callback->SetTotal(frame_.unpacked_size));
            const Int32 mode = test ? NArchive::NExtract::NAskMode::kTest : NArchive::NExtract::NAskMode::kExtract;
            Int32 result = NArchive::NExtract::NOperationResult::kOK;
            {
                ComOwner<ISequentialOutStream> destination;
                Check(callback->GetStream(0, destination.Address(), mode));
                if (!test && !destination.Get()) return S_OK;
                Check(callback->PrepareOperation(mode));
                StreamSink sink(test ? nullptr : destination.Get());
                StreamReader reader(stream_);
                try {
                    const auto current = vzip::Inspect(reader);
                    if (current.format != expected_) throw vzip::Error(vzip::ErrorCode::Header, "Input format changed");
                    vzip::Decode(reader, current, sink, [&](uint64_t, uint64_t output) {
                        const UInt64 completed = output;
                        Check(callback->SetCompleted(&completed));
                    });
                } catch (const vzip::Error& error) {
                    result = error.code == vzip::ErrorCode::Crc ?
                        NArchive::NExtract::NOperationResult::kCRCError :
                        NArchive::NExtract::NOperationResult::kDataError;
                }
            } // Release output before notifying the host of the operation result.
            return callback->SetOperationResult(result);
        });
    }
    HRESULT STDMETHODCALLTYPE GetNumberOfProperties(UInt32* count) noexcept override {
        if (!count) return E_POINTER;
        *count = UInt32(std::size(kProperties)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyInfo(UInt32 index, BSTR* name, PROPID* id, VARTYPE* type) noexcept override {
        return PropertyInfo(kProperties, std::size(kProperties), index, name, id, type);
    }
    HRESULT STDMETHODCALLTYPE GetNumberOfArchiveProperties(UInt32* count) noexcept override {
        if (!count) return E_POINTER;
        *count = UInt32(std::size(kArchiveProperties)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetArchivePropertyInfo(UInt32 index, BSTR* name, PROPID* id, VARTYPE* type) noexcept override {
        return PropertyInfo(kArchiveProperties, std::size(kArchiveProperties), index, name, id, type);
    }
};
}
extern "C" HRESULT WINAPI GetNumberOfFormats(UInt32* count) noexcept {
    if (!count) return E_POINTER;
    *count = 2; return S_OK;
}
extern "C" HRESULT WINAPI GetHandlerProperty2(UInt32 index, PROPID id, PROPVARIANT* value) noexcept {
    if (!value) return E_POINTER;
    PropVariantInit(value);
    if (index >= 2) return E_INVALIDARG;
    using namespace NArchive::NHandlerPropID;
    switch (id) {
    case kName: return Text(value, index == 0 ? L"VZip" : L"VZstd");
    case kClassID:
        value->bstrVal = SysAllocStringByteLen(reinterpret_cast<const char*>(&kClasses[index]), sizeof(GUID));
        if (!value->bstrVal) return E_OUTOFMEMORY;
        value->vt = VT_BSTR; return S_OK;
    case kExtension: return Text(value, index == 0 ? L"vz" : L"vsz vz");
    case kSignature: {
        const char* signature = index == 0 ? "VZa" : "VSZa";
        value->bstrVal = SysAllocStringByteLen(signature, index == 0 ? 3 : 4);
        if (!value->bstrVal) return E_OUTOFMEMORY;
        value->vt = VT_BSTR; return S_OK;
    }
    case kUpdate: value->vt = VT_BOOL; value->boolVal = VARIANT_FALSE; return S_OK;
    default: return S_OK;
    }
}
extern "C" HRESULT WINAPI CreateObject(const GUID* class_id, const GUID* iid, void** object) noexcept {
    return Boundary([&]() -> HRESULT {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (!class_id || !iid) return E_POINTER;
        if (*iid != IID_IInArchive) return E_NOINTERFACE;
        for (unsigned index = 0; index < 2; ++index) {
            if (*class_id == kClasses[index]) {
                *object = static_cast<IInArchive*>(new Handler(index == 0 ? vzip::Format::VZa : vzip::Format::VSZa));
                return S_OK;
            }
        }
        return CLASS_E_CLASSNOTAVAILABLE;
    });
}

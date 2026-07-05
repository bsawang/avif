// avif_wic2.cpp - AVIF WIC Decoder using avifdec.exe
// WIC decoders use IStream, but for thumbnail purposes we use the
// shell's IShellItem2::GetDisplayName to get the file path.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

// WIC Bitmap Decoder CLSID {DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0D}
const CLSID CLSID_AvifWicDecoder =
    { 0xdf4c9a6e, 0x5b3a, 0x4f2a, { 0x9e, 0x8c, 0x7d, 0x1e, 0x3f, 0x2a, 0x5b, 0x0d } };

// WIC IIDs - define locally to avoid uuid.lib dependency
// IID_IWICBitmapSource: {56A9A988-6B24-4960-9E83-06A7B08F7E84}
static const IID IID_IWICBitmapSource =
    { 0x56a9a988, 0x6b24, 0x4960, { 0x9e, 0x83, 0x06, 0xa7, 0xb0, 0x8f, 0x7e, 0x84 } };
// IID_IWICBitmapDecoder: {9EDDE9E7-8DEE-47ea-99FE-E6D5F0C5D5C4}
static const IID IID_IWICBitmapDecoder =
    { 0x9edde9e7, 0x8dee, 0x47ea, { 0x99, 0xfe, 0xe6, 0xd5, 0xf0, 0xc5, 0xd5, 0xc4 } };
// IID_IWICBitmapFrameDecode: {E50B2FEE-AA3E-46FF-8715-9A83A4E6A1F8} (or similar)
static const IID IID_IWICBitmapFrameDecode =
    { 0xe50b2fee, 0xaa3e, 0x46ff, { 0x87, 0x15, 0x9a, 0x83, 0xa4, 0xe6, 0xa1, 0xf8 } };

static HMODULE g_hModule = NULL;
static LONG g_refCount = 0;

// Debug trace
static void Trace(const char* msg) {
    OutputDebugStringA(msg);
    FILE* f = fopen("C:\\Windows\\Temp\\AvifWIC_trace.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
}

// ========== Avif Frame (implements IWICBitmapFrameDecode for WIC) ==========
class AvifFrame : public IWICBitmapFrameDecode
{
public:
    AvifFrame() : m_ref(1) { InterlockedIncrement(&g_refCount); }
    ~AvifFrame() { InterlockedDecrement(&g_refCount); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG; *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IWICBitmapSource || riid == IID_IWICBitmapFrameDecode)
            *ppv = static_cast<IWICBitmapFrameDecode*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this; return c;
    }

    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override
        { *pw = m_w; *ph = m_h; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* p) override
        { *p = GUID_WICPixelFormat32bppBGRA; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetResolution(double*, double*) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette*) override
        { return WINCODEC_ERR_PALETTEUNAVAILABLE; }

    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT stride, UINT bufSize, BYTE* buf) override
    {
        if (!m_pixels) return E_FAIL;
        UINT y0 = prc ? prc->Y : 0;
        UINT copyH = prc ? prc->Height : m_h;
        UINT copyW = prc ? prc->Width : m_w;
        for (UINT y = 0; y < copyH && (y + y0) < m_h; y++) {
            memcpy(buf + y * stride, m_pixels + (y + y0) * m_w * 4, std::min(stride, copyW * 4u));
        }
        return S_OK;
    }

    // IWICBitmapFrameDecode stubs
    HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(IWICMetadataQueryReader**) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetColorContexts(UINT, IWICColorContext**, UINT*) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetThumbnail(IWICBitmapSource**) override
        { return WINCODEC_ERR_CODECNOTHUMBNAIL; }

    // Load from PNG file on disk
    bool LoadPNG(const wchar_t* pngPath)
    {
        Gdiplus::GdiplusStartupInput gs;
        ULONG_PTR token;
        if (Gdiplus::GdiplusStartup(&token, &gs, NULL) != Gdiplus::Ok) return false;

        Gdiplus::Bitmap bmp(pngPath);
        m_w = bmp.GetWidth();
        m_h = bmp.GetHeight();
        if (m_w == 0 || m_h == 0) { Gdiplus::GdiplusShutdown(token); return false; }

        Gdiplus::Rect r(0, 0, m_w, m_h);
        Gdiplus::BitmapData bd;
        if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
            Gdiplus::GdiplusShutdown(token); return false;
        }
        size_t sz = (size_t)m_w * m_h * 4;
        m_pixels = new BYTE[sz];
        for (UINT y = 0; y < m_h; y++)
            memcpy(m_pixels + y * m_w * 4, (BYTE*)bd.Scan0 + y * bd.Stride, m_w * 4);
        bmp.UnlockBits(&bd);
        Gdiplus::GdiplusShutdown(token);
        return true;
    }

private:
    LONG m_ref;
    UINT m_w = 0, m_h = 0;
    BYTE* m_pixels = nullptr;
};

// ========== Decoder (implements IWICBitmapDecoder just enough for thumbnails) ==========
class AvifDecoder : public IWICBitmapDecoder
{
public:
    AvifDecoder() : m_ref(1) { InterlockedIncrement(&g_refCount); }
    ~AvifDecoder() { InterlockedDecrement(&g_refCount); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG; *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IWICBitmapDecoder)
            *ppv = static_cast<IWICBitmapDecoder*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this; return c;
    }

    // IWICBitmapDecoder - only these matter for thumbnails:
    HRESULT STDMETHODCALLTYPE Initialize(IStream* pStream, WICDecodeOptions) override;
    HRESULT STDMETHODCALLTYPE QueryCapability(IStream* s, DWORD* pCap) override
        { Trace("AvifDecoder::QueryCapability"); *pCap = WICBitmapDecoderCapabilityCanDecodeAllImages; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* p) override
        { Trace("AvifDecoder::GetContainerFormat"); *p = GUID_ContainerFormatHeif; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDecoderInfo(IWICBitmapDecoderInfo** pp) override
        { Trace("AvifDecoder::GetDecoderInfo"); *pp = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette* p) override
        { Trace("AvifDecoder::CopyPalette"); return WINCODEC_ERR_PALETTEUNAVAILABLE; }
    HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(IWICMetadataQueryReader** pp) override
        { Trace("AvifDecoder::GetMetadataQueryReader"); *pp = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPreview(IWICBitmapSource** pp) override
        { Trace("AvifDecoder::GetPreview"); *pp = nullptr; return WINCODEC_ERR_CODECNOTHUMBNAIL; }
    HRESULT STDMETHODCALLTYPE GetThumbnail(IWICBitmapSource** pp) override
        { Trace("AvifDecoder::GetThumbnail"); *pp = nullptr; return WINCODEC_ERR_CODECNOTHUMBNAIL; }
    HRESULT STDMETHODCALLTYPE GetColorContexts(UINT n, IWICColorContext** pp, UINT* pn) override
        { Trace("AvifDecoder::GetColorContexts"); if (pn) *pn = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFrameCount(UINT* pCount) override
        { Trace("AvifDecoder::GetFrameCount"); *pCount = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFrame(UINT idx, IWICBitmapFrameDecode** ppFrame) override;

private:
    LONG m_ref;
    WCHAR m_filePath[MAX_PATH] = {0};
    AvifFrame* m_frame = nullptr;
};

HRESULT AvifDecoder::Initialize(IStream* pStream, WICDecodeOptions)
{
    Trace("AvifDecoder::Initialize");
    // Get file path from stream
    STATSTG stat = {0};
    HRESULT hr = pStream->Stat(&stat, STATFLAG_DEFAULT);
    if (FAILED(hr) || !stat.pwcsName) { Trace("  Stat failed"); return E_FAIL; }

    StringCchCopyW(m_filePath, MAX_PATH, stat.pwcsName);
    CoTaskMemFree(stat.pwcsName);

    if (GetFileAttributesW(m_filePath) == INVALID_FILE_ATTRIBUTES) {
        Trace("  File not found");
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    Trace("  File OK");
    return S_OK;
}

HRESULT AvifDecoder::GetFrame(UINT idx, IWICBitmapFrameDecode** ppFrame)
{
    Trace("AvifDecoder::GetFrame");
    if (idx != 0 || !ppFrame) return E_INVALIDARG;
    *ppFrame = NULL;
    if (m_filePath[0] == 0) return E_FAIL;

    // Find avifdec.exe
    WCHAR decPath[MAX_PATH];
    GetModuleFileNameW(g_hModule, decPath, MAX_PATH);
    WCHAR* slash = wcsrchr(decPath, L'\\');
    if (!slash) return E_FAIL;
    StringCchCopyW(slash + 1, MAX_PATH - (slash - decPath) - 1, L"avifdec.exe");
    if (GetFileAttributesW(decPath) == INVALID_FILE_ATTRIBUTES)
        StringCchCopyW(decPath, MAX_PATH, L"C:\\Program Files\\AvifThumbHandler\\avifdec.exe");
    if (GetFileAttributesW(decPath) == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    // Temp PNG
    WCHAR tempDir[MAX_PATH], tempPNG[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempDir)) return E_FAIL;
    GetTempFileNameW(tempDir, L"awi", 0, tempPNG);
    size_t tlen = wcslen(tempPNG);
    StringCchCopyW(tempPNG + tlen - 4, 5, L".png");

    // avifdec.exe input.avif output.png
    WCHAR cmd[4096];
    StringCchPrintfW(cmd, 4096, L"\"%s\" \"%s\" \"%s\"", decPath, m_filePath, tempPNG);
    Trace("  Launching avifdec.exe...");

    PROCESS_INFORMATION pi = {0};
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!pi.hProcess) { DeleteFileW(tempPNG); return E_FAIL; }
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (ec != 0 || GetFileAttributesW(tempPNG) == INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(tempPNG); return E_FAIL;
    }

    AvifFrame* frame = new (std::nothrow) AvifFrame();
    if (!frame) { DeleteFileW(tempPNG); return E_OUTOFMEMORY; }
    if (!frame->LoadPNG(tempPNG)) {
        delete frame; DeleteFileW(tempPNG); return E_FAIL;
    }
    DeleteFileW(tempPNG);

    HRESULT hr = frame->QueryInterface(IID_IWICBitmapFrameDecode, (void**)ppFrame);
    frame->Release();  // Release the initial ref, caller owns the QI ref
    return hr;
}

// ========== Class Factory ==========
class Factory : public IClassFactory {
    LONG m_ref = 1;
public:
    Factory() { InterlockedIncrement(&g_refCount); }
    ~Factory() { InterlockedDecrement(&g_refCount); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG; *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) *ppv = static_cast<IClassFactory*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this; return c;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) override {
        if (pOuter) return CLASS_E_NOAGGREGATION;
        AvifDecoder* p = new (std::nothrow) AvifDecoder();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

// ========== Exports ==========
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (rclsid != CLSID_AvifWicDecoder) return CLASS_E_CLASSNOTAVAILABLE;
    Factory* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return (g_refCount == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer() { return S_OK; }
extern "C" HRESULT __stdcall DllUnregisterServer() { return S_OK; }

BOOL APIENTRY DllMain(HMODULE hm, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_hModule = hm; DisableThreadLibraryCalls(hm); }
    return TRUE;
}

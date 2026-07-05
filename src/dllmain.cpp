// AvifThumbCpp.dll - Native C++ COM ShellEx for AVIF thumbnails
// Compiled with MSVC, uses libavif inline decode
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <shobjidl.h>       // IInitializeWithFile, IInitializeWithStream
#include <objbase.h>
#include <strsafe.h>
#include <cstdio>
#include <algorithm>
#include <gdiplus.h>
#include <avif/avif.h>

// Debug logging - uses ANSI file I/O
static void LogMsg(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    StringCbVPrintfW(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
    // Write UTF-8 to log file (use %TEMP% for compatibility with dllhost)
    WCHAR logPath[MAX_PATH];
    GetEnvironmentVariableW(L"TEMP", logPath, MAX_PATH);
    wcscat_s(logPath, MAX_PATH, L"\\AvifThumbCpp.log");
    FILE* f = _wfopen(logPath, L"a");
    if (f) {
        char mb[2048];
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, mb, 2048, NULL, NULL);
        fprintf(f, "%s\n", mb);
        fflush(f);
        fclose(f);
    }
}

// ---------- GUIDs ----------
// {DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}
const CLSID CLSID_AvifThumbProvider =
    { 0xdf4c9a6e, 0x5b3a, 0x4f2a, { 0x9e, 0x8c, 0x7d, 0x1e, 0x3f, 0x2a, 0x5b, 0x0c } };

// Original photo thumbnail provider: {C7657C4A-9F68-40fa-A4DF-96BC08EB3551}
const CLSID CLSID_PhotoMetadataHandler =
    { 0xc7657c4a, 0x9f68, 0x40fa, { 0xa4, 0xdf, 0x96, 0xbc, 0x08, 0xeb, 0x35, 0x51 } };

// IID_IInitializeWithFile
const IID IID_IInitializeWithFile =
    { 0xb7d14566, 0x0509, 0x4cce, { 0xa7, 0x1f, 0x0a, 0x55, 0x42, 0x33, 0xbd, 0x9b } };

// IID_IThumbnailProvider
const IID IID_IThumbnailProvider =
    { 0xe357fccd, 0xa995, 0x4576, { 0xb0, 0x1f, 0x23, 0x46, 0x30, 0x15, 0x4e, 0x96 } };

// IID_IInitializeWithStream: {b824b49d-22ac-4161-ac8a-9916e8fa3f7f}
const IID IID_IInitializeWithStream =
    { 0xb824b49d, 0x22ac, 0x4161, { 0xac, 0x8a, 0x99, 0x16, 0xe8, 0xfa, 0x3f, 0x7f } };

// ---------- Module state ----------
static HMODULE g_hModule = NULL;
static LONG    g_moduleRefCount = 0;
static bool    g_gdiplusStarted = false;
static ULONG_PTR g_gdiplusToken = 0;
static CRITICAL_SECTION g_gdiplusLock;

static HRESULT EnsureGdiplus()
{
    EnterCriticalSection(&g_gdiplusLock);
    if (!g_gdiplusStarted) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::Status st = Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL);
        if (st == Gdiplus::Ok) {
            g_gdiplusStarted = true;
        } else {
            LeaveCriticalSection(&g_gdiplusLock);
            return E_FAIL;
        }
    }
    LeaveCriticalSection(&g_gdiplusLock);
    return S_OK;
}

// Forward declarations
class CThumbProvider;
class CClassFactory;


// ---------- Thumbnail Provider ----------
class CThumbProvider : public IInitializeWithFile, public IInitializeWithStream, public IThumbnailProvider, public IExtractImage2
{
public:
    CThumbProvider() : m_refCount(1), m_filePath(NULL), m_iconSize(0), m_streamData()
        { InterlockedIncrement(&g_moduleRefCount); }
    ~CThumbProvider()
        { if (m_filePath) CoTaskMemFree(m_filePath);
          if (m_streamData.data) avifRWDataFree(&m_streamData);
          InterlockedDecrement(&g_moduleRefCount); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IInitializeWithFile)
            *ppv = static_cast<IInitializeWithFile*>(this);
        else if (riid == IID_IInitializeWithStream)
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (riid == IID_IThumbnailProvider)
            *ppv = static_cast<IThumbnailProvider*>(this);
        else if (riid == __uuidof(IExtractImage) || riid == __uuidof(IExtractImage2))
            *ppv = static_cast<IExtractImage2*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef()
        { return InterlockedIncrement(&m_refCount); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        LONG c = InterlockedDecrement(&m_refCount);
        if (c == 0) delete this;
        return c;
    }

    // IInitializeWithFile
    IFACEMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD)
    {
        if (!pszFilePath) { LogMsg(L"Initialize: null path"); return E_INVALIDARG; }
    LogMsg(L"Initialize: %s", pszFilePath);
        if (m_filePath) CoTaskMemFree(m_filePath);
    size_t len = wcslen(pszFilePath) + 1;
        m_filePath = (LPWSTR)CoTaskMemAlloc(len * sizeof(WCHAR));
        if (!m_filePath) return E_OUTOFMEMORY;
        wcscpy_s(m_filePath, len, pszFilePath);
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pStream, DWORD)
    {
        LogMsg(L"IInitializeWithStream::Initialize");
        if (!pStream) return E_INVALIDARG;

        // Read stream into memory buffer (no temp file needed with libavif)
        BYTE buf[65536];
        avifRWData data = AVIF_DATA_EMPTY;
        while (true) {
            ULONG cbRead = 0;
            HRESULT hr = pStream->Read(buf, sizeof(buf), &cbRead);
            if (FAILED(hr) || cbRead == 0) break;
            size_t oldSize = data.size;
            if (avifRWDataRealloc(&data, oldSize + cbRead) != AVIF_RESULT_OK) {
                avifRWDataFree(&data);
                LogMsg(L"  realloc failed");
                return E_OUTOFMEMORY;
            }
            memcpy(data.data + oldSize, buf, cbRead);
            data.size = oldSize + cbRead;
        }
        if (data.size == 0) {
            LogMsg(L"  empty stream");
            return E_FAIL;
        }
        m_streamData = data;  // Transfer ownership
        LogMsg(L"  read %zu bytes", m_streamData.size);
        return S_OK;
    }

    // IExtractImage
    IFACEMETHODIMP GetLocation(LPWSTR pszPathBuffer, DWORD cch,
        DWORD* pdwPriority, const SIZE* prgSize, DWORD dwRecClrDepth,
        DWORD* pdwFlags)
    {
        LogMsg(L"IExtractImage::GetLocation size=%dx%d", prgSize->cx, prgSize->cy);
        if (m_filePath) {
            wcscpy_s(pszPathBuffer, cch, m_filePath);
        }
        m_iconSize = prgSize->cx;
        *pdwFlags = IEIFLAG_OFFLINE;
        return S_OK;
    }

    IFACEMETHODIMP Extract(HBITMAP* phBmpThumbnail)
    {
        LogMsg(L"IExtractImage::Extract cx=%u", m_iconSize);
        WTS_ALPHATYPE alpha;
        return GetThumbnail(m_iconSize, phBmpThumbnail, &alpha);
    }

    // IExtractImage2
    IFACEMETHODIMP GetDateStamp(FILETIME* pDateStamp)
    {
        *pDateStamp = { 0 };
        return E_NOTIMPL;
    }

private:
    LONG m_refCount;
    LPWSTR m_filePath;
    avifRWData m_streamData;
    UINT m_iconSize;
};

// ---------- Class Factory ----------
class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : m_refCount(1)
        { InterlockedIncrement(&g_moduleRefCount); }
    ~CClassFactory()
        { InterlockedDecrement(&g_moduleRefCount); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
            *ppv = static_cast<IClassFactory*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef()
        { return InterlockedIncrement(&m_refCount); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        LONG c = InterlockedDecrement(&m_refCount);
        if (c == 0) delete this;
        return c;
    }

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
    {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CThumbProvider* p = new (std::nothrow) CThumbProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL) { return S_OK; }

private:
    LONG m_refCount;
};

// ---------- Helper: delegate to original PhotoMetadataHandler ----------
typedef HRESULT (__stdcall *DllGetClassObjectFunc)(REFCLSID, REFIID, void**);

static HRESULT DelegateToPhotoThumbnail(const wchar_t* filePath, UINT cx,
    HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
    LogMsg(L"  delegating to PhotoMetadataHandler: %s", filePath);

    wchar_t sys32[MAX_PATH];
    GetSystemDirectoryW(sys32, MAX_PATH);
    wcscat_s(sys32, MAX_PATH, L"\\PhotoMetadataHandler.dll");

    HMODULE hDll = LoadLibraryW(sys32);
    if (!hDll) {
        LogMsg(L"  LoadLibrary failed: %u", GetLastError());
        return E_FAIL;
    }

    DllGetClassObjectFunc getClassObj = (DllGetClassObjectFunc)
        GetProcAddress(hDll, "DllGetClassObject");
    if (!getClassObj) {
        LogMsg(L"  DllGetClassObject not found");
        FreeLibrary(hDll);
        return E_FAIL;
    }

    IClassFactory* pFactory = nullptr;
    HRESULT hr = getClassObj(CLSID_PhotoMetadataHandler,
        IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr)) {
        LogMsg(L"  getClassObj failed: hr=0x%08x", hr);
        FreeLibrary(hDll);
        return hr;
    }

    IInitializeWithFile* pInit = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IInitializeWithFile, (void**)&pInit);
    pFactory->Release();
    if (FAILED(hr)) {
        LogMsg(L"  CreateInstance failed: hr=0x%08x", hr);
        FreeLibrary(hDll);
        return hr;
    }

    hr = pInit->Initialize(filePath, 0);
    if (FAILED(hr)) {
        LogMsg(L"  Init failed: hr=0x%08x", hr);
        pInit->Release();
        FreeLibrary(hDll);
        return hr;
    }

    IThumbnailProvider* pThumb = nullptr;
    hr = pInit->QueryInterface(IID_IThumbnailProvider, (void**)&pThumb);
    if (FAILED(hr)) {
        LogMsg(L"  QI thumb failed: hr=0x%08x", hr);
        pInit->Release();
        FreeLibrary(hDll);
        return hr;
    }

    hr = pThumb->GetThumbnail(cx, phbmp, pdwAlpha);
    LogMsg(L"  Photo GetThumbnail: hr=0x%08x", hr);

    pThumb->Release();
    pInit->Release();
    FreeLibrary(hDll);
    return hr;
}

// ---------- Check if file has .avif extension ----------
static bool IsAvifFile(const wchar_t* path)
{
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    return (_wcsicmp(dot, L".avif") == 0);
}

// ---------- GetThumbnail implementation (libavif inline) ----------
HRESULT CThumbProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
    *phbmp = NULL;
    *pdwAlpha = WTSAT_UNKNOWN;

    LogMsg(L"GetThumbnail: cx=%u path=%s", cx, m_filePath ? m_filePath : L"null");

    // Try stream data first (IInitializeWithStream path)
    if (m_streamData.data && m_streamData.size > 0)
        goto do_decode;

    // Fall back to file path (IInitializeWithFile path)
    if (!m_filePath) { LogMsg(L"  no filepath and no stream data"); return E_FAIL; }

    if (GetFileAttributesW(m_filePath) == INVALID_FILE_ATTRIBUTES) {
        LogMsg(L"  file not found: %s", m_filePath);
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    // Delegate non-AVIF files to original PhotoMetadataHandler
    if (!IsAvifFile(m_filePath)) {
        LogMsg(L"  not AVIF, delegating");
        return DelegateToPhotoThumbnail(m_filePath, cx, phbmp, pdwAlpha);
    }

do_decode:
    // --- libavif inline decode ---

    avifRWData raw = AVIF_DATA_EMPTY;
    if (m_streamData.data && m_streamData.size > 0) {
        raw = m_streamData;
        m_streamData.data = NULL;
        m_streamData.size = 0;
        LogMsg(L"  using stream data: %zu bytes", raw.size);
    } else {
        HANDLE hFile = CreateFileW(m_filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            LogMsg(L"  CreateFile failed: %u", GetLastError());
            return E_FAIL;
        }
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == 0 || fileSize > 50 * 1024 * 1024) {
            LogMsg(L"  invalid file size: %u", fileSize);
            CloseHandle(hFile);
            return E_FAIL;
        }
        if (avifRWDataRealloc(&raw, fileSize) != AVIF_RESULT_OK) {
            LogMsg(L"  avifRWDataRealloc failed");
            CloseHandle(hFile);
            return E_OUTOFMEMORY;
        }
        DWORD cbRead = 0;
        ReadFile(hFile, raw.data, fileSize, &cbRead, NULL);
        raw.size = cbRead;
        CloseHandle(hFile);
    }

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) {
        LogMsg(L"  avifDecoderCreate failed");
        avifRWDataFree(&raw);
        return E_OUTOFMEMORY;
    }

    avifResult result = avifDecoderSetIOMemory(decoder, raw.data, raw.size);
    if (result != AVIF_RESULT_OK) {
        LogMsg(L"  avifDecoderSetIOMemory failed: %d", result);
        avifDecoderDestroy(decoder);
        avifRWDataFree(&raw);
        return E_FAIL;
    }

    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        LogMsg(L"  avifDecoderParse failed: %d", result);
        avifDecoderDestroy(decoder);
        avifRWDataFree(&raw);
        return E_FAIL;
    }

    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        LogMsg(L"  avifDecoderNextImage failed: %d", result);
        avifDecoderDestroy(decoder);
        avifRWDataFree(&raw);
        return E_FAIL;
    }

    avifImage* image = decoder->image;
    LogMsg(L"  decoded: %ux%u depth=%u", image->width, image->height, image->depth);

    avifRGBImage rgb;
    memset(&rgb, 0, sizeof(rgb));
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_BGRA;
    rgb.depth = 8;
    rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_BILINEAR;
    avifRGBImageAllocatePixels(&rgb);

    result = avifImageYUVToRGB(image, &rgb);
    if (result != AVIF_RESULT_OK) {
        LogMsg(L"  avifImageYUVToRGB failed: %d", result);
        avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(decoder);
        avifRWDataFree(&raw);
        return E_FAIL;
    }

    UINT w = rgb.width;
    UINT h = rgb.height;
    LogMsg(L"  RGB: %ux%u", w, h);

    HRESULT hr = EnsureGdiplus();
    if (FAILED(hr)) {
        LogMsg(L"  GDI+ init failed");
        avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(decoder);
        avifRWDataFree(&raw);
        return hr;
    }

    Gdiplus::Bitmap srcBmp(w, h, rgb.rowBytes, PixelFormat32bppARGB, rgb.pixels);

    UINT thumbW, thumbH;
    if (w > h) {
        thumbW = cx;
        thumbH = std::max(1u, h * cx / w);
    } else {
        thumbH = cx;
        thumbW = std::max(1u, w * cx / h);
    }

    Gdiplus::Bitmap scaledBmp(thumbW, thumbH, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&scaledBmp);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(&srcBmp, 0, 0, thumbW, thumbH);
    g.Flush();

    Gdiplus::Color transparent(0, 0, 0, 0);
    Gdiplus::Status st = scaledBmp.GetHBITMAP(transparent, phbmp);

    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    avifRWDataFree(&raw);

    if (st != Gdiplus::Ok) {
        LogMsg(L"  GetHBITMAP failed: %d", st);
        return E_FAIL;
    }
    LogMsg(L"  SUCCESS: hbmp=0x%p", *phbmp);
    *pdwAlpha = WTSAT_RGB;
    return S_OK;
}

// ---------- DLL Exports ----------
extern "C" HRESULT __stdcall DllGetClassObject(
    REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (rclsid != CLSID_AvifThumbProvider) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* p = new (std::nothrow) CClassFactory();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return (g_moduleRefCount == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer()
{
    WCHAR dllPath[MAX_PATH];
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

    HKEY hk;
    LONG r;

    // CLSID
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}",
        0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    RegSetValueExW(hk, NULL, 0, REG_SZ, (BYTE*)L"AvifThumbHandler", 22);
    RegCloseKey(hk);

    // InprocServer32
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}\\InprocServer32",
        0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    DWORD cbData = (wcslen(dllPath) + 1) * sizeof(WCHAR);
    RegSetValueExW(hk, NULL, 0, REG_SZ, (BYTE*)dllPath, cbData);
    RegSetValueExW(hk, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Apartment", 20);
    RegCloseKey(hk);

    // ---- DisableProcessIsolation ----
    // Required for IInitializeWithFile to work in-process (avoids dllhost isolation).
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hk, NULL);
    if (r == ERROR_SUCCESS) {
        DWORD val = 1;
        RegSetValueExW(hk, L"DisableProcessIsolation", 0, REG_DWORD,
            (BYTE*)&val, sizeof(val));
        RegCloseKey(hk);
    }

    // ---- Remove system PropertyHandler for .avif ----
    // The built-in PhotoMetadataHandler/MSHEIF.dll property handler is slow on AVIF,
    // causing 6-7 second hangs in file dialogs. Our thumbnails don't need it.
    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\.avif");

    // ---- .avif extension registrations ----
    // IThumbnailProvider
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\.avif\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}",
        0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r == ERROR_SUCCESS) {
        RegSetValueExW(hk, NULL, 0, REG_SZ,
            (BYTE*)L"{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}",
            sizeof(L"{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}"));
        RegCloseKey(hk);
    }

    // IExtractImage (older fallback path)
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\.avif\\ShellEx\\{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}",
        0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r == ERROR_SUCCESS) {
        RegSetValueExW(hk, NULL, 0, REG_SZ,
            (BYTE*)L"{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}",
            sizeof(L"{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}"));
        RegCloseKey(hk);
    }

    // PerceivedType + ContentType
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\.avif",
        0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"PerceivedType", 0, REG_SZ,
            (BYTE*)L"image", 12);
        RegSetValueExW(hk, L"ContentType", 0, REG_SZ,
            (BYTE*)L"image/avif", 20);
        RegCloseKey(hk);
    }

    return S_OK;
}

extern "C" HRESULT __stdcall DllUnregisterServer()
{
    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\.avif\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\.avif\\ShellEx\\{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}");

    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}\\InprocServer32");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}");

    return S_OK;
}

// ---------- DLL Main ----------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_gdiplusLock);
        break;
    case DLL_PROCESS_DETACH:
        if (g_gdiplusStarted) Gdiplus::GdiplusShutdown(g_gdiplusToken);
        DeleteCriticalSection(&g_gdiplusLock);
        break;
    }
    return TRUE;
}

# AVIF Thumbnail Handler — Research & Investigation Record
# AVIF 资源管理器缩略图 — 探索过程记录

> **Date / 日期**: 2026-07-05 (Evening update / 晚间更新)
> **Environment / 环境**: Windows 10 Pro 22H2 (19045.4529)
> **Status / 状态**: ✅ AVIF thumbnails working (DisableProcessIsolation is the key / DisableProcessIsolation 是关键)

---

## Table of Contents / 目录

1. [Background / 需求背景](#1-background--需求背景)
2. [Explorer Thumbnail Mechanism / Explorer 缩略图机制](#2-explorer-thumbnail-mechanism--explorer-缩略图机制)
3. [Attempts / 方案探索](#3-attempts--方案探索)
   - 3.12 [MSVC 2022 Rebuild + IExtractImage2](#312-msvc-2022-rebuild--iextractimage2-support--msvc-2022-重新编译--iextractimage2-支持)
   - 3.13 [PhotoMetadataHandler Call Failure Analysis](#313-photometadatahandler-call-failure-analysis--photometadatahandler-内部调用失败分析)
   - 3.14 [WIC Decoder Path Verification](#314-wic-decoder-path-verification--wic-解码器路径验证)
4. [Summary of All Approaches / 方案总览](#4-summary-of-all-approaches--方案总览)
5. [Root Cause / 根本原因](#5-root-cause--根本原因)
6. [Solution: DisableProcessIsolation / 解决方案](#6-solution-disableprocessisolation--解决方案disableprocessisolation)
7. [Untried Directions / 未尝试方向](#7-untried-directions--未尝试方向不必要了)

---

## 1. Background / 需求背景

**CN:** `H:\comfyUI\ref\` 下有大量 AI 生成的 `.avif` 图片，希望在资源管理器（Explorer）中直接显示缩略图。

**EN:** There are many AI-generated `.avif` images under `H:\comfyUI\ref\`, and we want thumbnail previews directly in Windows Explorer.

## 2. Explorer Thumbnail Mechanism / Explorer 缩略图机制

**CN:** （见上午文档）

**EN:** (See morning document)

---

## 3. Attempts / 方案探索

### 3.1–3.11

**CN:** 见上午文档，11 种方案均失败。

**EN:** See morning document. All 11 approaches failed.

---

### 3.12 MSVC 2022 Rebuild + IExtractImage2 Support / MSVC 2022 重新编译 + IExtractImage2 支持

**CN:**

**起因**：怀疑 MinGW 编译的 DLL 与系统 COM 基础设施不兼容。vswhere 找不到 MSVC。

**做法**：
1. 下载 VS 2022 Build Tools (`vs_BuildTools.exe`) 并静默安装到 `C:\BuildTools`
2. 安装 Windows SDK 10.0.26100.0（含 wincodec.h 等头文件）
3. 修复 `build.bat` 中多处 bug：
   - `/DEF:"dllmain.cpp"` → 正确的 `.def` 文件
   - 去掉 `/NODEFAULTLIB`（导致 CRT 符号缺失）
   - 添加 `advapi32.lib`（注册表 API）
4. 添加 `exports.def` 文件，用 `PRIVATE` 标记导出函数
5. 从 `dllmain.cpp` 和 `avif_wic2.cpp` 中移除 `__declspec(dllexport)`，改用 `.def`
6. 给 `avif_wic2.cpp` 添加 `IExtractImage2` 接口支持（`GetLocation` + `Extract` 方法）
7. 修复 WIC 解码器空实现：
   - `QueryCapability` → 返回 `WICBitmapDecoderCapabilityCanDecodeAllImages`
   - `GetContainerFormat` → 返回 `GUID_ContainerFormatHeif`

**编译结果**：
- `AvifThumbCpp.dll` —— 184KB（原来 MinGW 版 143KB）
- `AvifWIC.dll` —— 154KB（原来 MinGW 版 147KB）

**测试结果**：
- ✅ **In-process COM activation works** (same as MinGW)
- ✅ **WIC decoder works via IWICImagingFactory API**
- ✅ **IShellItemImageFactory returns valid 256×256 thumbnails**
- ❌ **dllhost.exe still refuses to load DLL**
- ❌ **PhotoMetadataHandler still doesn't recognize AVIF**

**结论**：MSVC vs MinGW 对核心问题无影响。阻塞在 OS 层，不在编译器层。

---

**EN:**

**Motivation**: Suspected MinGW-compiled DLL was incompatible with system COM infrastructure. vswhere couldn't find MSVC.

**Actions**:
1. Downloaded VS 2022 Build Tools (`vs_BuildTools.exe`) and silently installed to `C:\BuildTools`
2. Installed Windows SDK 10.0.26100.0 (includes wincodec.h etc.)
3. Fixed multiple bugs in `build.bat`:
   - `/DEF:"dllmain.cpp"` → correct `.def` file referencing
   - Removed `/NODEFAULTLIB` (caused missing CRT symbols)
   - Added `advapi32.lib` (registry API)
4. Added `exports.def` with `PRIVATE` export marking
5. Removed `__declspec(dllexport)` from source files, switched to `.def` file
6. Added `IExtractImage2` support to `avif_wic2.cpp` (`GetLocation` + `Extract` methods)
7. Fixed WIC decoder stub implementations:
   - `QueryCapability` → returns `WICBitmapDecoderCapabilityCanDecodeAllImages`
   - `GetContainerFormat` → returns `GUID_ContainerFormatHeif`

**Build Results**:
- `AvifThumbCpp.dll` — 184KB (was 143KB for MinGW)
- `AvifWIC.dll` — 154KB (was 147KB for MinGW)

**Test Results**:
- ✅ In-process COM activation works (no difference from MinGW)
- ✅ WIC decoder works via IWICImagingFactory API
- ✅ IShellItemImageFactory returns valid 256×256 thumbnails
- ❌ dllhost.exe still refuses to load the DLL
- ❌ PhotoMetadataHandler still doesn't recognize AVIF

**Conclusion**: Compiler choice (MSVC vs MinGW) is not the issue. The blockage is at the OS layer.

---

### 3.13 PhotoMetadataHandler Call Failure Analysis / PhotoMetadataHandler 内部调用失败分析

**CN:**

**现象**：PhotoMetadataHandler 直接加载测试返回
```
Initialize: hr=0x00000000
GetThumbnail(256): hr=0xc00d36b4 (hbmp=NULL)
```

**错误码分析**：
- Facility = 0x0D = 13 = `FACILITY_MEDIASERVER`
- Severity = 1 (error)
- 这不是 WIC 错误（WIC facility = 0x898）

**推测**：PhotoMetadataHandler 不走公开的 `IWICImagingFactory` API，内部调用的是 **Media Foundation** 解码管道。这解释了为什么 WIC 解码器注册正确但 PhotoMetadataHandler 不识别——它就没去扫描 WIC decoder category。

**注册表验证**：
- `SystemFileAssociations\image\ShellEx\{e357fccd-...}` 拥有者是 **TrustedInstaller**
- 管理员只有 ReadKey 权限，无法写入
- 替换系统图片管道路径是不可能的

---

**EN:**

**Observation**: Direct PhotoMetadataHandler loading test returns
```
Initialize: hr=0x00000000
GetThumbnail(256): hr=0xc00d36b4 (hbmp=NULL)
```

**Error Code Analysis**:
- Facility = 0x0D = 13 = `FACILITY_MEDIASERVER`
- Severity = 1 (error)
- This is NOT a WIC error (WIC facility = 0x898)

**Hypothesis**: PhotoMetadataHandler does NOT use the public `IWICImagingFactory` API. Internally it uses the **Media Foundation** decoding pipeline. This explains why the WIC decoder registers correctly but PhotoMetadataHandler ignores it — it never scans the WIC decoder category.

**Registry Verification**:
- `SystemFileAssociations\image\ShellEx\{e357fccd-...}` is owned by **TrustedInstaller**
- Administrators only have ReadKey permissions, cannot write
- Replacing the system image handler path is impossible

---

### 3.14 WIC Decoder Path Verification / WIC 解码器路径验证

**CN:**

**核心发现**：WIC 解码器在 Win10 22H2 上**确实能正常工作**，但只通过 `IWICImagingFactory` 公开 API：

```
✅ IWICImagingFactory::CreateDecoderFromStream() → 成功自动检测 AVIF
✅ IWICImagingFactory::CreateDecoderFromFilename() → 成功解码
✅ IWICImagingFactory::CreateComponentEnumerator(WICBitmapDecoders) → 枚举包含我们的解码器
```

WIC 基础设施本身是开放的，第三方解码器可以注册并工作。问题是 Explorer 的缩略图管道**没有走这个 API**。

---

**EN:**

**Key Finding**: The WIC decoder **works correctly** on Windows 10 22H2, but only through the public `IWICImagingFactory` API:

```
✅ IWICImagingFactory::CreateDecoderFromStream() → auto-detects AVIF successfully
✅ IWICImagingFactory::CreateDecoderFromFilename() → decodes successfully
✅ IWICImagingFactory::CreateComponentEnumerator(WICBitmapDecoders) → enumerates our decoder
```

The WIC infrastructure is open — third-party decoders can register and work. The problem is that Explorer's thumbnail pipeline **does not go through this API**.

---

## 4. Summary of All Approaches / 方案总览

| # | Approach / 方案 | Type / 类型 | Result / 结果 | Failure Reason / 失败原因 |
|---|-----------------|-------------|---------------|--------------------------|
| 1–11 | See morning doc / 见上午 | Various / 各种 | ❌ | — |
| 12 | MSVC 2022 Rebuild / MSVC 2022 重编译 | Build / 自构建 | ❌ | Compiler-independent, OS guard / 编译器无关，OS 防护机制 |
| 13 | IExtractImage2 Implementation | COM Extension / COM 扩展 | ❌ | IExtractImage also goes through dllhost |
| 14 | Shell Extensions Approved / 注册表信任 | Registry / 注册表 | ❌ | Additional trust checks / 仍有其他信任检查 |
| 15 | **DisableProcessIsolation** | **Registry / 注册表** | **✅** | **Key fix / 关键修复** |

## 5. Root Cause / 根本原因

### dllhost Process Isolation / dllhost 进程隔离

**CN:** Windows 10 默认将缩略图处理器加载到 **dllhost.exe（COM Surrogate）** 隔离进程中运行。这个隔离进程只支持 `IInitializeWithStream` 接口。我们的实现使用 `IInitializeWithFile`（因为需要文件路径来调用 avifdec），导致 COM 在隔离进程中**静默失败**——根本不加载 DLL。

**EN:** Windows 10 loads thumbnail handlers in an isolated **dllhost.exe (COM Surrogate)** process by default. This isolated process only supports the `IInitializeWithStream` interface. Our implementation uses `IInitializeWithFile` (needed for the file path to spawn avifdec), causing COM to **silently fail** in the isolated process — the DLL is never loaded.

### Path Analysis / 路径分析

```
Path B / 路径 B：.avif\ShellEx\{e357fccd-...} (IThumbnailProvider)
  → Registration correct, but dllhost isolation rejects loading / 注册正确，但 dllhost 隔离进程拒绝加载 ⛔
  → Cause: uses IInitializeWithFile instead of IInitializeWithStream / 因为用了 IInitializeWithFile 而非 IInitializeWithStream
```

### PhotoMetadataHandler Doesn't Use Our WIC Decoder

- Even though the WIC decoder is fully functional via the public `IWICImagingFactory` API / 即使 WIC 解码器通过公开 API 完全可用
- PhotoMetadataHandler uses the Media Foundation pipeline internally / PhotoMetadataHandler 内部使用 Media Foundation 管道
- Error `0xc00d36b4` is a MediaServer error (unrelated to this issue) / 是 MediaServer 错误（与本次问题无关）

### WIC Decoder Registration Has No Effect on Thumbnail Pipeline

- WIC decoder registered at `HKLM\...\WIC\Decoders\.avif` is complete and functional / WIC 解码器注册完整且工作
- But Explorer's thumbnail pipeline doesn't decode through `IWICImagingFactory` / 但 Explorer 缩略图管道不通过 IWICImagingFactory 解码

## 6. Solution: DisableProcessIsolation / 解决方案

### Fix / 修复方法

**CN:** 在 CLSID 注册表项下添加 `DisableProcessIsolation = 1`（REG_DWORD）：

**EN:** Add `DisableProcessIsolation = 1` (REG_DWORD) under the CLSID registry key:

```
HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}
    DisableProcessIsolation = 1 (REG_DWORD)
```

### Why It Works / 为什么有效

**CN:** Microsoft 官方文档 [Building Thumbnail Providers](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers) 明确说明：

**EN:** Microsoft's official documentation [Building Thumbnail Providers](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers) clearly states:

> *"If your thumbnail handler does not implement IInitializeWithStream, it must opt out of running in the isolated process. To opt out, set DisableProcessIsolation = 1."*

**CN:** 设置后，Explorer 直接在自身进程内加载我们的 DLL，不走 dllhost.exe。`IInitializeWithFile` 接口正常工作。

**EN:** After setting this, Explorer loads our DLL directly in its own process, bypassing dllhost.exe. The `IInitializeWithFile` interface works normally.

### Final Result / 最终效果

- **CN:** Explorer 直接调用 `IThumbnailProvider::GetThumbnail(cx=256)`，每次调用：avifdec.exe → PNG → GDI+ 缩放 → HBITMAP。所有 AVIF 文件缩略图正常显示。
- **EN:** Explorer calls `IThumbnailProvider::GetThumbnail(cx=256)` directly. Each call: avifdec.exe → PNG → GDI+ resize → HBITMAP. All AVIF file thumbnails display correctly.

## 7. Untried Directions / 未尝试方向（不必要了）

### 7.1 IExtractImage Path / IExtractImage 路径

**CN:** DLL 已实现 `IExtractImage2` 接口，但 IExtractImage 也走 dllhost 隔离。`DisableProcessIsolation` 已经解决了问题。

**EN:** The DLL already implements `IExtractImage2`, but IExtractImage also goes through dllhost isolation. `DisableProcessIsolation` already solves the problem.

### 7.2 Digital Signature / COM Permissions / DLL Injection / Win11 etc.

**CN:** 所有其他方向都不需要了。

**EN:** All other directions are unnecessary.

### 7.3 Caveats / 注意事项

- **CN:** `DisableProcessIsolation` 仅用于调试/开发环境，微软不建议生产环境使用。我们的场景是自用工作站，可接受此限制。如果 DLL 崩溃，可能会连带 Explorer 一起崩溃（相比之下，走 dllhost 只会崩溃 dllhost 进程）。
- **EN:** `DisableProcessIsolation` is intended for debugging/development only; Microsoft does not recommend it for production. For our personal workstation this is acceptable. If the DLL crashes, it may also crash Explorer (whereas with dllhost, only dllhost would crash).

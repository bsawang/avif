# AVIF 资源管理器缩略图 — 探索过程记录

> **日期**：2026-07-05（晚间更新）
> **环境**：Windows 10 Pro 22H2 (19045.4529)
> **状态**：✅ 已实现 AVIF 缩略图（DisableProcessIsolation 是关键）
>
> [English version](RESEARCH.en.md)

---

## 目录

1. [需求背景](#1-需求背景)
2. [Explorer 缩略图机制](#2-explorer-缩略图机制)
3. [方案探索](#3-方案探索)
   - 3.12 [MSVC 2022 重新编译 + IExtractImage2 支持](#312-msvc-2022-重新编译--iextractimage2-支持)
   - 3.13 [PhotoMetadataHandler 内部调用失败分析](#313-photometadatahandler-内部调用失败分析)
   - 3.14 [WIC 解码器路径验证](#314-wic-解码器路径验证)
4. [方案总览](#4-方案总览)
5. [根本原因](#5-根本原因)
6. [解决方案：DisableProcessIsolation](#6-解决方案disableprocessisolation)
7. [未尝试方向](#7-未尝试方向不必要了)
8. [附录：开发者参考](#8-附录开发者参考)
   - 8.1 [架构说明](#81-架构说明)
   - 8.2 [注册表配置](#82-注册表配置)
   - 8.3 [从源码构建](#83-从源码构建)
   - 8.4 [实现细节](#84-实现细节)

---

## 1. 需求背景

`H:\comfyUI\ref\` 下有大量 AI 生成的 `.avif` 图片，希望在资源管理器（Explorer）中直接显示缩略图。

## 2. Explorer 缩略图机制

（见上午文档 — 概括了 Explorer 通过 COM 加载缩略图提供器、IThumbnailProvider 接口，以及 dllhost.exe COM Surrogate 隔离机制。）

---

## 3. 方案探索

### 3.1–3.11

见上午文档，11 种方案均失败。包括 WIC 解码器注册、TreatAs 重定向、PerceivedType、ContentType、ProgID 等各类注册表方案。

---

### 3.12 MSVC 2022 重新编译 + IExtractImage2 支持

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
- ✅ **进程内 COM 激活正常**（与 MinGW 无本质区别）
- ✅ **WIC 解码器通过 IWICImagingFactory API 正常工作**
- ✅ **IShellItemImageFactory 返回 256×256 有效缩略图**
- ❌ **dllhost.exe 仍不加载 DLL**
- ❌ **PhotoMetadataHandler 仍不识别**

**结论**：MSVC vs MinGW 对核心问题无影响。阻塞在 OS 层，不在编译器层。

---

### 3.13 PhotoMetadataHandler 内部调用失败分析

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

### 3.14 WIC 解码器路径验证

**核心发现**：WIC 解码器在 Win10 22H2 上**确实能正常工作**，但只通过 `IWICImagingFactory` 公开 API：

```
✅ IWICImagingFactory::CreateDecoderFromStream() → 成功自动检测 AVIF
✅ IWICImagingFactory::CreateDecoderFromFilename() → 成功解码
✅ IWICImagingFactory::CreateComponentEnumerator(WICBitmapDecoders) → 枚举包含我们的解码器
```

WIC 基础设施本身是开放的，第三方解码器可以注册并工作。问题是 Explorer 的缩略图管道**没有走这个 API**。

---

## 4. 方案总览

| # | 方案 | 类型 | 结果 | 失败原因 |
|---|------|------|------|---------|
| 1–11 | 见上午 | 各种 | ❌ | — |
| 12 | MSVC 2022 重编译 | 自构建 | ❌ | 编译器无关，OS 防护机制 |
| 13 | IExtractImage2 实现 | COM 扩展 | ❌ | IExtractImage 也走 dllhost |
| 14 | Shell Extensions Approved | 注册表 | ❌ | 仍有其他信任检查 |
| 15 | **DisableProcessIsolation** | **注册表** | **✅** | **关键修复** |

## 5. 关键根因：dllhost 进程隔离

Windows 10 默认将缩略图处理器加载到 **dllhost.exe（COM Surrogate）** 隔离进程中运行。这个隔离进程只支持 `IInitializeWithStream` 接口。我们的实现使用 `IInitializeWithFile`（因为需要文件路径来调用 avifdec），导致 COM 在隔离进程中**静默失败**——根本不加载 DLL。

### 路径分析

```
路径 B：.avif\ShellEx\{e357fccd-...} (IThumbnailProvider)
  → 注册正确，但 dllhost 隔离进程拒绝加载 ⛔
  → 是因为用了 IInitializeWithFile 而非 IInitializeWithStream
```

### PhotoMetadataHandler 不使用我们的 WIC 解码器

- 即使 WIC 解码器通过 `IWICImagingFactory` 的公开 API 完全可用
- PhotoMetadataHandler 内部使用 Media Foundation 管道
- 0xc00d36b4 是 MediaServer 错误（与本次问题无关）

### WIC 解码器注册对缩略图管道无影响

- WIC 解码器注册在 `HKLM\...\WIC\Decoders\.avif` 完整且工作
- 但 Explorer 缩略图管道不通过 `IWICImagingFactory` 解码

---

## 6. 解决方案：DisableProcessIsolation

### 修复方法

在 CLSID 注册表项下添加 `DisableProcessIsolation = 1`（REG_DWORD）：

```
HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}
    DisableProcessIsolation = 1 (REG_DWORD)
```

### 为什么有效

Microsoft 官方文档 [Building Thumbnail Providers](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers) 明确说明：

> *"If your thumbnail handler does not implement IInitializeWithStream, it must opt out of running in the isolated process. To opt out, set DisableProcessIsolation = 1."*

设置后，Explorer 直接在自身进程内加载我们的 DLL，不走 dllhost.exe。`IInitializeWithFile` 接口正常工作。

### 最终效果

- **Explorer 直接调用 `IThumbnailProvider::GetThumbnail(cx=256)`**
- 每次调用：avifdec.exe → PNG → GDI+ 缩放 → HBITMAP
- 所有 AVIF 文件缩略图正常显示

---

## 7. 未尝试方向（不必要了）

### 7.1 IExtractImage 路径（不必要）

DLL 已实现 `IExtractImage2` 接口，但 IExtractImage 也走 dllhost 隔离。`DisableProcessIsolation` 已经解决了问题。

### 7.2 数字签名 / COM 权限 / DLL 注入 / Win11 等

所有其他方向都不需要了。

### 7.3 注意事项

- `DisableProcessIsolation` 仅用于调试/开发环境，微软不建议生产环境使用
- 我们的场景是自用工作站，可接受此限制
- 如果 DLL 崩溃，可能会连带 Explorer 一起崩溃（相比之下，走 dllhost 只会崩溃 dllhost 进程）

---

## 8. 附录：开发者参考

### 8.1 架构说明

本方案包含两个 COM 组件：

| 组件 | 源文件 | 用途 |
|------|--------|------|
| **缩略图提供器** | `src/dllmain.cpp` | COM ShellEx：`IThumbnailProvider` + `IExtractImage2` + `IInitializeWithFile` |
| **WIC 解码器** | `src/avif_wic2.cpp` | WIC 位图解码器（已注册但缩略图路径不使用） |

缩略图通过启动 **avifdec.exe** 子进程生成（而非直接链接 libavif），保持 DLL 小巧，COM 架构简单。

### 8.2 注册表配置

| 注册表路径 | 值 |
|-----------|-----|
| `CLSID\{DF4C9A6E-...C}\InprocServer32`（默认值） | `C:\Windows\System32\AvifThumbCpp.dll` |
| `CLSID\{...}\InprocServer32\ThreadingModel` | `Apartment` |
| `CLSID\{...}\DisableProcessIsolation` | `1` (REG_DWORD) |
| `.avif\ShellEx\{e357fccd-...}` | `{DF4C9A6E-...C}` (IThumbnailProvider) |
| `.avif\ShellEx\{BB2E617C-...}` | `{DF4C9A6E-...C}` (IExtractImage 兼容) |

### 8.3 从源码构建

**构建要求：**
- Visual Studio 2022 Build Tools（或完整版 VS 2022）
  - 工作负载："使用 C++ 的桌面开发"
  - Windows SDK 10.0.26100.0+
- libavif 工具（`avifdec.exe`）— 已包含在 `bin/` 目录

**构建：**

```cmd
cd src
build.bat
```

构建脚本会自动检测 MSVC 安装路径。

**构建产物：**
- `AvifThumbCpp.dll` — 缩略图处理器
- `AvifWIC.dll` — WIC 解码器（可选，缩略图不需要）

调试时可查看跟踪日志：`C:\Windows\Temp\AvifThumbCpp.log`

### 8.4 实现细节

- 实现三个 COM 接口以获得最大兼容性：`IThumbnailProvider`（现代）、`IExtractImage2`（旧版兼容）和 `IInitializeWithFile`（初始化）
- 非 AVIF 文件委托给系统的 `PhotoMetadataHandler.dll` 处理
- WIC 解码器（`AvifWIC.dll`）是独立组件，可通过 `IWICImagingFactory` API 工作，但**不**被资源管理器缩略图管道使用

---

[English version](RESEARCH.en.md)

# AVIF 资源管理器缩略图 — 探索过程记录

> **日期**：2026-07-06（最终更新）
> **环境**：Windows 10 Pro 22H2 (19045.4529)
> **版本**：v2 — libavif 内联解码 + 文件对话框性能修复
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
   - 3.15 [libavif 内联解码（替换 avifdec 子进程）](#315-libavif-内联解码替换-avifdec-子进程)
   - 3.16 [PropertyHandler 导致文件对话框卡顿](#316-propertyhandler-导致文件对话框卡顿)
4. [方案总览](#4-方案总览)
5. [根本原因](#5-根本原因)
6. [解决方案](#6-解决方案)
7. [未尝试方向](#7-未尝试方向)
8. [附录：开发者参考](#8-附录开发者参考)

---

## 1. 需求背景

`H:\comfyUI\ref\` 下有大量 AI 生成的 `.avif` 图片，希望在资源管理器（Explorer）中直接显示缩略图。

## 2. Explorer 缩略图机制

Windows 通过 COM Shell Extension 加载缩略图提供器。主要路径：
- **IThumbnailProvider**（现代接口，推荐）
- **IExtractImage2**（旧版兼容）
- 默认在 **dllhost.exe**（COM Surrogate）隔离进程中加载
- 隔离进程只支持 `IInitializeWithStream`

---

## 3. 方案探索

### 3.1–3.11

见上午文档，11 种方案均失败。包括 WIC 解码器注册、TreatAs 重定向、PerceivedType、ContentType、ProgID 等各类注册表方案。

---

### 3.12 MSVC 2022 重新编译 + IExtractImage2 支持

**起因**：怀疑 MinGW 编译的 DLL 与系统 COM 基础设施不兼容。

**做法**：
1. 下载 VS 2022 Build Tools 并安装到 `C:\BuildTools`
2. 安装 Windows SDK 10.0.26100.0
3. 修复 `build.bat` 中多处 bug
4. 添加 `exports.def` 文件
5. 给 DLL 添加 `IExtractImage2` 接口支持

**结论**：编译器选择（MSVC vs MinGW）不是核心问题。阻塞在 OS 层。

---

### 3.13 PhotoMetadataHandler 内部调用失败分析

**现象**：PhotoMetadataHandler 返回错误代码 `0xc00d36b4`（MediaServer 错误）

**推测**：PhotoMetadataHandler 不走公开的 IWICImagingFactory API，内部调用 Media Foundation 解码管道。

---

### 3.14 WIC 解码器路径验证

**核心发现**：WIC 解码器在 Win10 22H2 上能正常工作，但只通过 `IWICImagingFactory` 公开 API。Explorer 的缩略图管道不走这个 API。

---

### 3.15 libavif 内联解码（替换 avifdec 子进程）

**起因**：原始方案使用 `CreateProcess("avifdec.exe")` 生成缩略图，每次调用 200-500ms。合并调用 `IInitializeWithStream` 需求后，需要通过 C/C++ API 直接解码。

**做法**：
1. 编译 libavif v1.4.2 + dav1d v1.5.3 为静态库（`avif.lib`，4.3MB）
2. 用 CMake + Meson + Ninja 构建，启用 `AVIF_CODEC_DAV1D=ON`
3. 替换 `CreateProcess` 代码块为 `avifDecoderCreate` → `avifDecoderSetIOMemory` → `avifDecoderParse` → `avifDecoderNextImage` → `avifImageYUVToRGB`
4. 添加 `IInitializeWithStream` 接口支持，流数据读入内存后直接解码（无需临时文件）
5. DLL 从 184KB → 1.9MB（内联解码器，无可执行依赖）

**结果**：解码时间从 200-500ms 降低到 10-50ms。不再需要 avifdec.exe。

---

### 3.16 PropertyHandler 导致文件对话框卡顿

**起因**：文件对话框（Common File Dialog / IFileOpenDialog）在 AVIF 文件夹打开时卡 6-7 秒，期间显示"正在处理"。此问题不影响资源管理器。

**调研**：
1. 注册 `PerceivedType=image` 后，Windows 自动为 `.avif` 关联属性处理器
2. `PropertyHandlers\.avif` → `PhotoMetadataHandler.dll` → `MSHEIF.dll`（Media Foundation 解码器）
3. 文件对话框打开时对每个文件读取属性（`System.Image.Dimensions` 等），触发此慢路径
4. 该 MF 解码器首次加载/初始化需要 6-7 秒
5. 资源管理器不读取图片属性，因此不受影响

**修复**：删除 `PropertyHandlers\.avif` 注册。缩略图不需要这个处理器。

**结果**：文件对话框秒开。

---

## 4. 方案总览

| # | 方案 | 类型 | 结果 | 失败原因 |
|---|------|------|------|---------|
| 1–11 | 见上午 | 各种 | ❌ | — |
| 12 | MSVC 2022 重编译 | 构建 | ❌ | 编译器无关，OS 防护机制 |
| 13 | IExtractImage2 实现 | COM 扩展 | ❌ | IExtractImage 也走 dllhost |
| 14 | Shell Extensions Approved | 注册表 | ❌ | 仍有其他信任检查 |
| 15 | **DisableProcessIsolation** | **注册表** | **✅** | **关键修复** |
| 16 | **libavif 内联解码** | **代码** | **✅** | 替代子进程，速度提升 10x |
| 17 | **删除 PropertyHandler** | **注册表** | **✅** | 消除文件对话框卡顿 |

## 5. 根本原因

### 核心问题：dllhost 进程隔离

Windows 10 默认将缩略图处理器加载到 **dllhost.exe（COM Surrogate）** 隔离进程中运行。这个隔离进程只支持 `IInitializeWithStream` 接口。使用 `IInitializeWithFile` 的实现会导致 COM 在隔离进程中**静默失败**——根本不加载 DLL。

### 文件对话框卡顿

`PerceivedType=image` 触发了 Windows 图片属性处理器（`PhotoMetadataHandler` → `MSHEIF.dll`），文件对话框读取属性时触发慢速 Media Foundation 解码。资源管理器不读取属性，因此不受影响。

### 子进程开销

`avifdec.exe` 子进程方式每次解码 200-500ms，在需要批量生成缩略图时延迟累计明显。

## 6. 解决方案

### 6.1 DisableProcessIsolation

```reg
HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}
    DisableProcessIsolation = 1 (REG_DWORD)
```

设置后 Explorer 直接在自身进程内加载 DLL，绕过 dllhost.exe。

### 6.2 libavif 内联解码

编译 libavif + dav1d 为静态库，在 DLL 中直接调用 C API 解码 AVIF：
- `avifDecoderCreate()` → 创建解码器
- `avifDecoderSetIOMemory()` → 设置内存数据
- `avifDecoderParse()` → 解析文件
- `avifDecoderNextImage()` → 解码图像
- `avifImageYUVToRGB()` → 转 BGRA

### 6.3 删除 PropertyHandler 注册

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.avif
```

删除此项后文件对话框不再触发慢速图片属性读取。

## 7. 未尝试方向

### 7.1 IExtractImage 路径（不必要）

DLL 已实现 `IExtractImage2` 接口，但 IExtractImage 也走 dllhost 隔离。`DisableProcessIsolation` 已经解决了问题。

### 7.2 数字签名 / COM 权限 / DLL 注入 / Win11 等

所有其他方向都不需要了。

### 7.3 注意事项

- `DisableProcessIsolation` 仅用于调试/开发环境，微软不建议生产环境使用
- 如果 DLL 崩溃，可能会连带 Explorer 一起崩溃

---

## 8. 附录：开发者参考

### 8.1 架构

```
资源管理器（进程内加载）
  └─ AvifThumbCpp.dll (1.9MB, libavif 内联解码)
       └─ IInitializeWithFile / IInitializeWithStream
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ libavif C API 解码 → YUV
            └─ avifImageYUVToRGB → BGRA
            └─ GDI+ 缩放 → HBITMAP
```

| 接口 | 说明 |
|------|------|
| `IInitializeWithFile` | 资源管理器使用（DisableProcessIsolation 下） |
| `IInitializeWithStream` | dllhost 隔离进程使用 |
| `IThumbnailProvider` | 缩略图生成（主要路径） |
| `IExtractImage2` | 旧版兼容 |

### 8.2 注册表配置

| 注册表路径 | 值 |
|-----------|-----|
| `CLSID\{DF4C9A6E-...C}\InprocServer32`（默认值） | `C:\Windows\System32\AvifThumbCpp.dll` |
| `CLSID\{...}\InprocServer32\ThreadingModel` | `Apartment` |
| `CLSID\{...}\DisableProcessIsolation` | `1` (REG_DWORD) |
| `.avif\ShellEx\{e357fccd-...}` | `{DF4C9A6E-...C}` (IThumbnailProvider) |
| `.avif\ShellEx\{BB2E617C-...}` | `{DF4C9A6E-...C}` (IExtractImage 兼容) |

**注意**：`PropertyHandlers\.avif` 已被删除，不需要注册。

### 8.3 从源码构建

**构建要求：**
- Visual Studio 2022 Build Tools（或完整版 VS 2022）
  - 工作负载："使用 C++ 的桌面开发"
  - Windows SDK 10.0.26100.0+
- cmake、meson、ninja（`pip install cmake meson ninja`）
- nasm 2.16+（https://www.nasm.us/）
- libavif 源码 v1.4.2（包含在 `../libavif_src/`）

**构建 libavif 静态库：**
```cmd
mkdir build & cd build
cmake .. -G "Ninja" -DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF ^
    -DAVIF_CODEC_DAV1D=ON -DAVIF_LOCAL_DAV1D=ON ^
    -DCMAKE_C_FLAGS_RELEASE="/MD /O2 /GS-"
ninja
copy avif.lib ..\..\publish\lib\
```

**构建 DLL：**
```cmd
cd src
build.bat
```

**构建产物：**
- `AvifThumbCpp.dll` — 1.9MB 缩略图处理器（libavif 内联解码）

### 8.4 实现细节

- 通过 `IInitializeWithStream` 和 `IInitializeWithFile` 两个初始化接口获得最大兼容性
- 流数据读入 `avifRWData` 内存缓冲区，直接提交 libavif 解码（无临时文件）
- 非 AVIF 文件委托给系统的 `PhotoMetadataHandler.dll` 处理
- 解码后通过 GDI+ 缩放到请求尺寸，返回 HBITMAP

### 8.5 调试

日志位置：`%TEMP%\AvifThumbCpp.log`（由 `IInitializeWithStream::Initialize` 触发时写入）或 `C:\Windows\Temp\AvifThumbCpp.log`（Explorer 进程内加载时写入）。

---

[English version](RESEARCH.en.md)

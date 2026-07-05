# Windows 10 资源管理器 AVIF 缩略图支持

![Windows 10](https://img.shields.io/badge/Windows_10-22H2-0078D6?logo=windows)
![C++](https://img.shields.io/badge/C++-COM-00599C?logo=cplusplus)
![MSVC](https://img.shields.io/badge/MSVC-2022-800080)
![GitHub Release](https://img.shields.io/github/v/release/bsawang/avif?logo=github)
![License](https://img.shields.io/badge/License-Non--Commercial_|_Dual-red)

原生 C++ COM Shell 扩展，为 Windows 10 资源管理器添加 **AVIF 缩略图预览** 支持。

> **状态**: Windows 10 22H2 测试通过，MSVC 2022 编译。
>
> [English version](README.en.md)

---

## Windows 版本说明

| 版本 | AVIF 支持情况 |
|------|--------------|
| **Windows 10** | ❌ 无原生 AVIF 缩略图支持 — **本工具可以解决** |
| **Windows 11** | ✅ 自带原生 AVIF 支持，开箱即用 |

Windows 11 已内置 AVIF 编解码器支持，可直接显示缩略图。本工具主要面向 **Windows 10 用户**，无需升级系统即可获得 AVIF 缩略图预览。

---

## 快速开始

### 环境要求

- Windows 10（推荐 22H2）
- 管理员权限（COM 注册需要）

### 安装方法

**方式 A：安装脚本（推荐）**

1. 下载[最新发布包](https://github.com/bsawang/avif/releases)
2. 解压到任意目录
3. **右键** `install.bat` → **以管理员身份运行**
4. 完成 — 打开 AVIF 文件夹即可看到缩略图

**方式 B：手动安装**

```cmd
:: 复制 DLL
copy bin\AvifThumbCpp.dll C:\Windows\System32\

:: 复制解码器
mkdir "C:\Program Files\AvifThumbHandler"
copy bin\avifdec.exe "C:\Program Files\AvifThumbHandler\"

:: 注册 COM 组件
regsvr32 C:\Windows\System32\AvifThumbCpp.dll

:: 重启资源管理器
taskkill /f /im explorer.exe & start explorer.exe
```

### 清理缩略图缓存

如果缩略图没有立即显示，请以管理员身份运行 `clear_cache.bat`。

### 常见问题

**问：缩略图还是不显示？**
- 检查调试日志：`C:\Windows\Temp\AvifThumbCpp.log`
- 清理缩略图缓存：`clear_cache.bat`
- 确认解码器存在：`C:\Program Files\AvifThumbHandler\avifdec.exe`

**问：安装时提示文件被锁定？**
因为 Explorer 会加载 DLL，安装时可能提示文件占用。脚本会自动重启 Explorer。如果失败，请关闭所有资源管理器窗口后重试。

### 工作原理

```
资源管理器（进程内加载）
  └─ AvifThumbCpp.dll
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ avifdec.exe（子进程）
                 └─ 解码 .avif → PNG
            └─ GDI+ 加载 PNG → 缩放 → 输出位图
```

本实现使用 `IInitializeWithFile` 接口，因此需要设置 `DisableProcessIsolation = 1`。完整开发者参考（架构、源码构建、注册表配置）见 [RESEARCH.md](RESEARCH.md)。

## 许可证

**双授权模式** — 详见 [LICENSE](LICENSE)。

- ✅ **非商业用途** — 免费使用，需保留版权声明
- ❌ **商业用途** — 需取得授权，请联系 **bsawang@126.com**

本工具使用了 libavif（BSD）、dav1d（BSD）、libyuv（BSD）及 IJG 等第三方组件。

## 编程环境

本项目完全使用 AI 辅助编程完成：

| 工具 | 版本 |
|------|------|
| VS Code | 1.127.0 |
| Claude Code | 2.1.168 |
| DeepSeek | V4 Flash |
| 编译器 | MSVC 14.44 (VS 2022 17.14) |
| 操作系统 | Windows 10 Pro 22H2 |

---

[English version](README.en.md)

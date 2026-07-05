# Changelog

## v1.0.1 (2026-07-06)

### 性能优化
- **libavif 内联解码**：替换 avifdec.exe 子进程方案，解码时间从 200-500ms 降至 10-50ms
- **文件对话框卡顿修复**：删除系统 PhotoMetadataHandler PropertyHandler，消除 6-7 秒"正在处理"延迟

### 新增功能
- **IInitializeWithStream 支持**：实现流式初始化接口，兼容 dllhost COM Surrogate 隔离进程
- **双初始化路径**：同时保留 IInitializeWithFile（资源管理器）和 IInitializeWithStream（dllhost）

### 移除
- 移除 `avifdec.exe`（12MB 外部依赖）
- 移除 `AvifWIC.dll`（空壳 WIC 解码器）
- 移除 `avif_wic2.cpp`（不再需要）

### 其他
- 项目结构精简，单源文件 dllmain.cpp ~630 行
- 添加 avif.h 头文件依赖
- 添加 lib/avif.lib 构建支持（暂不跟踪到仓库）
- 添加 uninstall.bat（兼容 v1.0.0 清理）
- 更新所有文档（README、RESEARCH）

---

## v1.0.0 (2026-07-05)

### 初始发布
- 原生 C++ COM Shell 扩展，IThumbnailProvider + IExtractImage2 + IInitializeWithFile
- avifdec.exe 子进程解码
- DisableProcessIsolation=1 注册表修复
- 非 AVIF 文件委托 PhotoMetadataHandler 处理
- WIC 解码器（AvifWIC.dll）配套注册

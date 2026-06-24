<p align="center">
  <img src="assets/Banner.png" alt="MAGDA" width="400">
</p>

<p align="center">
  <a href="https://github.com/Conceptual-Machines/magda-core/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Conceptual-Machines/magda-core/ci.yml?label=Linux&logo=linux&logoColor=white" alt="Linux Build"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Conceptual-Machines/magda-core/ci.yml?label=macOS&logo=apple" alt="macOS Build"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Conceptual-Machines/magda-core/ci.yml?label=Windows&logo=windows" alt="Windows Build"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20">
  <a href="https://crowdin.com/project/magda"><img src="https://badges.crowdin.net/magda/localized.svg" alt="Crowdin"></a>
</p>

<p align="center">
  数字音频工作站
</p>
<p align="center"><img src="assets/treaktion-engine-logo.png" alt="Powered by Tracktion Engine" width="250" height="80"></p>

---
MAGDA是一款免费的, 深度集成 AI 的开源数字音频工作站（DAW），基于 C++20、JUCE 框架与 Tracktion 音频引擎开发.
 - [English](README.md) | **简体中文** 

### 特性

- **多功能轨道**: 所有轨道同时支持音频片段与 MIDI 片段
- **三视图**: 实时循环乐段网格，走带和混音
- **AI指令**: 自然语言指令可在软件内直接生成并执行自定义领域专用语言（自带密钥接入第三方 API）
- **调制系统**: 每个乐器 / 效果单元与机架均配备 16 个低频振荡器（附带贝塞尔曲线编辑器）及 16 个宏旋钮
- **机架**: 支持多条并行处理链路，每条链路独立配备音量、声像、静音、独奏控制，机架可无限嵌套
- **钢琴窗**: 配备pitchbend和MIDI CC通道
- **鼓步进网格编辑器** 原生支持
- **会话视窗**: 支持片段触发播放
- **混音视窗**: 配备音量推子、声像、静音、独奏、效果发送通道以及输入输出信号路由功能
- **可折叠、可自由缩放面板**: 左侧为属性检查器与 AI 对话面板，右侧是插件浏览器与采样素材浏览器，底部为上下文自适应编辑器

### 更多链接

- **官网**: [magda.land](https://magda.land/)
- **YouTube**: [介绍](https://www.youtube.com/watch?v=momhIo5vOSc)
- **KVR**: [发布页](https://www.kvraudio.com/product/magda-by-conceptual-machines)
- **Issues**: [Bug反馈和特性请求](https://github.com/Conceptual-Machines/magda-core/issues)

## 状态

阅读[Issues](https://github.com/Conceptual-Machines/magda-core/issues) 获取已知Bug和计划

## 构建

### Prerequisites前置准备

- C++20 标准编译器（GCC 10 及以上、Clang 12 及以上，或 Xcode）
- CMake 3.20 及以上版本
- [Git LFS](https://git-lfs.com/) ：必需，用于拉取项目内置二进制资源，如中日韩字体等
  (CJK font, etc.).
macos下载 `brew install git-lfs`   ,
Debian/Ubuntu下载 `apt install git-lfs`   ,
Windows下载 `choco install git-lfs`   ,
然后每台设备执行一次 `git lfs install` 完成初始化

### Quick Start快速上手

```bash
# Clone with submodules and LFS assets
git clone --recursive https://github.com/Conceptual-Machines/magda-core.git
cd magda-core
git lfs pull  # safety net if git-lfs wasn't installed at clone time

# Setup and build
make setup
make debug

# Run
make run
```

### Make Targets编译构建目标

```bash
make setup      # Initialize submodules and dependencies
make debug      # Debug build
make release    # Release build
make test       # Run tests
make clean      # Clean build artifacts
make format     # Format code
make lint       # Run clang-tidy analysis
```

## Automated Workflows自动化操作

本项目内置自动化 GitHub Actions 工作流:

- **CI Workflow**: 每次代码推送自动执行，用于校验构建与代码质量
- **Security Scanning**: CodeQL 分析、密钥检测和漏洞扫描和第三方依赖漏洞扫描
- **Periodic Code Analysis**: 每周扫描 TODO、FIXME 与代码异味
- **Refactoring Scanner**: 每两周分析代码复杂度，统计技术债务

有关自动化分析和周期性工作流的详细信息，详见 [docs/AUTOMATED_WORKFLOWS.md](docs/AUTOMATED_WORKFLOWS.md) 

## Security安全机制

MAGDA 高度重视安全问题，代码仓库内置了全方位安全防护机制:

- 🔒 **分支保护**: 主分支启用保护机制，代码合并需经过指定审核与状态校验
- 🔍 **自动安全扫描**: 通过 CodeQL 对 C++ 代码开展漏洞安全分析
- 🔐 **密钥检测**: 自动扫描代码，杜绝账号密钥等敏感信息泄露
- 🛡️ **依赖监控**: 使用 Dependabot 自动跟进依赖包安全更新
- ⚡ **CI/CD 安全管控**: 所有安全检测项必须全部通过，代码才可合并

**发现安全问题？** 请查阅我们的安全政策 [安全政策](SECURITY.md) 以了解负责任的披露方式.

有关分支保护和安全架构的详细信息，请参见 [docs/BRANCH_PROTECTION.md](docs/BRANCH_PROTECTION.md).

## Architecture系统架构

```
magda/
├── daw/        # DAW application (C++/JUCE)
│   ├── audio/      # Audio processing
│   ├── core/       # Track, clip, selection management
│   ├── engine/     # Tracktion Engine wrapper
│   ├── interfaces/ # Abstract interfaces
│   ├── profiling/  # Performance profiling
│   ├── project/    # Project management and serialization
│   ├── ui/         # User interface components
│   └── utils/      # Utility helpers
└── agents/     # Agent system (C++)
tests/          # Test suite
scripts/        # Development and build scripts
docs/           # Documentation
```


## Dependencies项目依赖组件

- [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) - Audio engine
- [JUCE](https://juce.com/) - C++ application framework (GUI, audio I/O, plugin hosting, MIDI, DSP)
- [juce-llm](https://github.com/Conceptual-Machines/juce-llm) - LLM API client module
- [llama.cpp](https://github.com/ggml-org/llama.cpp) - Embedded local LLM inference
- [Catch2](https://github.com/catchorg/Catch2) - Testing (fetched via CMake)

## Issues

> **提醒:**  MAGDA还在早期v0的开发阶段。始发于2026年1月，先是进行了内部迭代，最近才公开发布。因为开发者们非常活跃，并且我们能预料到肯定会有bug和丢失的部分，所以帮助我们的最好方法就是提交您的问题

发现了一个bug或者有一个特性请求? 请在Github上 [open an issue](https://github.com/Conceptual-Machines/magda-core/issues/new)

## 开源证书

GPL v3 - see [LICENSE](LICENSE) for details.

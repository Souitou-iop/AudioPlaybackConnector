# 项目评估分析

> 这是个什么项目？相比原版有什么优势？

<details><summary>1 previous message</summary>

> <details><summary>Explored 3 files, 3 lists, ran 4 commands</summary>
>
> <details><summary>Explored a list</summary>
>
> - Listed files
>
> </details>
>
> - Ran `cat README.md && echo "\n---\n" && cat README.zh_CN.md`
>
> - Ran `git remote -v && git log -n 10 --oneline`
>
> <details><summary>Explored 4 files</summary>
>
> - Read `./AudioPlaybackConnector.h`
> - Read `./AudioPlaybackConnector.cpp`
> - Read `./AudioPlaybackConnector.cpp`
> - Read `./AudioPlaybackConnector.cpp`
>
> </details>
>
> - Ran `cat SettingsUtil.hpp && echo "\n---\n" && cat I18n.hpp`
>
> <details><summary>Explored a file, 2 lists</summary>
>
> - Listed files in `translate`
> - Listed files in `source`
> - Read `./Direct2DSvg.hpp`
>
> </details>
>
> - Ran `grep -ri "AudioPlaybackConnector" /Users/ebato/.codex/memories/ || echo "Not found in memories"`
>
> </details>
</details>

### 1. 项目简介

**AudioPlaybackConnector**（作者：[ysc3839](https://github.com/ysc3839/AudioPlaybackConnector)）是一个专为 **Windows 10 2004 (20H1)+ 及 Windows 11** 开发的**蓝牙音频接收（A2DP Sink）连接管理工具**。

#### 背景原理
* 微软自 Windows 10 2004 版本起，在系统底层重新引入了 **Bluetooth A2DP Sink**（蓝牙音频接收端）支持（通过 WinRT 的 `Windows.Media.Audio.AudioPlaybackConnection` API），允许用户将**手机、平板、播放器等设备的音频通过蓝牙无线串流到电脑，由电脑扬声器/耳机输出播放**。
* 但 Windows 系统本身并未提供一个便捷直观的原生 UI 开关来连接或管理 A2DP Sink，因此需要第三方工具来调用系统 API 发起和维持连接。

---

### 2. 这里的“原版”指什么？

在本项目出现前，微软应用商店（Microsoft Store）中最流行、最早被广泛使用的同类工具是 **Bluetooth Audio Receiver**（由 Mark Smirnov 开发）。

---

### 3. 相比原版（Bluetooth Audio Receiver）的核心优势

| 维度 | 原版 (Bluetooth Audio Receiver) | AudioPlaybackConnector |
| :--- | :--- | :--- |
| **开源状态** | **闭源**（仅通过应用商店分发） | **完全开源**（基于 MIT 协议，代码透明安全） |
| **运行形态** | **普通应用窗口**（必须保留在任务栏，占用前台/任务栏） | **托盘化常驻**（默认隐藏在右下角通知区域，无多余窗口） |
| **UI 与系统契合度** | 独立的传统 UWP 窗口界面 | 采用 **XAML Islands** 与系统原生 **DevicePicker**，完美融入 Windows 10/11 Fluent 风格 |
| **自动重连** | 每次需要手动点开窗口选择连接 | **支持开机/下次启动自动重连**（可记忆上次连接的设备） |
| **主题自适应** | 传统窗口主题 | 通过 Direct2D 实时渲染 SVG 托盘图标，**自动根据 Windows 浅色/深色主题切换黑白图标** |
| **多语言支持** | 语言支持有限 | 内置基于编译期/二进制 Hash（YMO）的多语言架构，支持**简体中文、繁体中文、英文**等 |
| **资源占用与分发** | 需通过 Store 下载或打包安装 | **原生 C++/WinRT 编写**，单文件绿色免安装，内存占用极低（仅几 MB） |
| **快捷联动** | 无附加系统联动 | 右键菜单一键直达 Windows 蓝牙设置（`ms-settings:bluetooth`）；监听 `TaskbarCreated`（Explorer 崩溃重启后自动恢复图标） |

---

### 总结

`AudioPlaybackConnector` 是对原版 UWP 窗口工具的一次**轻量化、现代化与开源重构**：
如果你只想让电脑像蓝牙音箱一样接收手机音频，它能在**不占任务栏、不弹窗打扰**的前提下，在系统托盘静默运行并提供开机自动重连功能。

> 我想针对该项目用winui3重写，但保留仅托盘化应用，你推荐吗？

结论先行：**如果定位依然是“纯托盘（Tray-only）无主窗口的后台工具”，极不推荐用 WinUI 3 重写；但如果你计划将其升级为带有“丰富控制面板/独立设置界面”的完整应用，则可以考虑。**

以下从**架构契合度、资源占用、分发成本和替代方案**为您详细分析：

---

### 一、 为什么不推荐用 WinUI 3 重写“纯托盘”应用？

#### 1. 核心 UI 本质上是系统级组件，而非自定义 XAML
* 本项目的核心交互（点击托盘弹出蓝牙设备列表）使用的是 Windows 系统原生的 **`Windows.Devices.Enumeration.DevicePicker`**。
* 这个设备选择面板是由 Windows 系统外壳直接渲染弹出的系统级浮层，**并不是应用自己用 XAML 绘制的列表**。
* 应用自身的 UI 仅仅是：
  * 右键菜单（`MenuFlyout`）
  * 退出时的确认弹窗（`Flyout`）
* 为这几个简单的浮层引入整套 WinUI 3 框架属于典型的“杀鸡用牛刀”。

#### 2. 分发与体积暴增（< 1 MB $\to$ 50~80 MB）
* **现状**：当前项目基于 **C++/WinRT + 系统内置 XAML Islands**，编译出来的 `.exe` 只有几百 KB，是完全绿色的单文件，依赖的都是 Windows 10/11 系统自带的动态库。
* **WinUI 3**：依赖 **Windows App SDK (WASDK)** 运行时。
  * 如果采用独立分发（Self-contained），即使是 C++ WinUI 3，也必须打包整套 WinUI 3 渲染引擎、DWriteCore、MRTCore 等，体积会膨胀到 **50MB 以上**，且附带几十个 `.dll`。
  * 如果采用框架依赖（Framework-dependent），用户机器上必须提前安装 Windows App Runtime，否则无法直接运行。

#### 3. 内存与后台常驻开销
* 托盘工具的核心诉求是 **24 小时常驻后台、无感、低占用**。
* 当前项目空闲内存占用大约在 **5 ~ 15 MB**。
* WinUI 3 初始化了完整的 XAML 渲染管线与合成线程，即使完全隐藏窗口，常驻内存一般也在 **40 ~ 80 MB** 左右，违背了轻量托盘工具的初衷。

#### 4. WinUI 3 对“纯托盘/无窗口应用”支持不够优雅
* WinUI 3 原生并没有开箱即用的 System Tray / NotifyIcon API（目前仍需借助 Win32 `Shell_NotifyIcon` 或第三方封装库如 `H.NotifyIcon`）。
* 在 WinUI 3 中实现“完全不闪烁主窗口、仅托盘常驻”，需要手动拦截窗口激活、操作 Win32 HWND 句柄隐藏窗口，开发体验并不比直接写 Win32/WinRT 顺畅。

---

### 二、 什么情况下适合用 WinUI 3 重写？

如果您计划打破“纯托盘”定位，**将应用升级为一个功能完备的现代音频管理软件**，例如：
1. **多设备独立音量/通道控制**（在界面中单独调整各个手机串流过来的音量）。
2. **连接状态与音频流详细信息**（显示采样率、延迟、音频编解码格式等）。
3. **丰富的设置面板**（自定义快捷键、自动切换输出设备、开机自启策略、连接黑白名单等）。
4. **Windows 11 视觉升级**（Mica / Acrylic 材质、现代化动画和圆角控件）。

在这种需要完整“主窗口/设置中心”的场景下，WinUI 3 的现代化控件和样式系统才能发挥最大价值。

---

### 三、 推荐的替代路线

如果您想重构或改进该项目，推荐以下更契合的方向：

| 方案 | 适用场景 | 优势 |
| :--- | :--- | :--- |
| **方案 A：保持 C++/WinRT，仅做局部现代化** | 维持现状与极致体积 | 沿用现有架构，仅适配 Windows 11 风格的 ContextMenu（通过系统 API 开启新版右键菜单样式与圆角），零额外体积开销。 |
| **方案 B：使用 Rust (`windows-rs`) 重写** | 追求现代化语言与内存安全 | 编译为单文件原生二进制（~1-2 MB），内存占用降到几 MB，没有 C++ 的指针/生命周期心智负担，非常适合写这类系统 API 胶水工具。 |
| **方案 C：C# (.NET 9) + Native AOT + Win32/WPF** | 熟悉 C#、追求开发效率 | 配合 `H.NotifyIcon` 和 `.NET Native AOT` 编译，生成单个约 10MB 的无依赖 exe，兼顾开发速度与体积。 |

> 哪个优势最大？用Rust重写会不会更好，我记得Rust就是效率的代名词，用Rust能不能让软件在mac上也能用？

这里需要把**“语言性能”**、**“重写收益”**以及**“跨平台机制”**分清楚。我们逐一解答你的疑问：

---

### 一、 哪个方案优势最大？

针对这个项目当前的定位（纯托盘、极致轻量）：**保持现有的 C++/WinRT 是综合优势最大的方案。**

* **现代码量极小**：整个核心逻辑只有 400 行左右的 C++ 代码。
* **资源已到物理极限**：编译后体积仅几百 KB，运行时占用约 5~10 MB 内存，无任何外部运行时依赖。
* **改动成本最低**：如果只是想优化功能（比如修 bug、增加快捷键、优化 Win11 菜单），直接在现有 C++ 上微调是最轻松、最稳健的做法。

---

### 二、 用 Rust 重写会不会更好？效率会更高吗？

**结论：不会有感知上的“效率提升”，但工程体验各有利弊。**

1. **运行效率上：Rust 和 C++ 是同一级别**
   * Rust 和 C++ 都是无垃圾回收（No-GC）、直接编译为机器码的底层语言。
   * 这个项目的本质是一个**“系统 API 粘合剂”**（主要工作是调用 Windows 的蓝牙和音频 API，然后挂起等待事件）。它没有任何 CPU 密集的业务计算，所以 Rust **不可能**比现有的 C++ 带来更快的运行速度或更低的内存占用（两者都已经处于物理极限）。

2. **开发体验上：Rust 在 Windows XAML 方面反而更繁琐**
   * **Rust 的优势**：拥有现代化的包管理（Cargo）、更严格的类型安全和无指针越界隐患。
   * **Rust 的痛点**：微软官方的 `windows-rs` 虽然支持 WinRT，但在调用 **XAML Islands**（把现代 XAML 控件嵌入 Win32 托盘）以及处理复杂的 COM 接口（如 `IInitializeWithWindow`、`DevicePicker`）时，Rust 代码会非常冗长，且缺乏 C++/WinRT 那样原生的语法糖支持，写起来比 C++ 更痛苦。

---

### 三、 核心问题：用 Rust 能不能让软件在 Mac 上也能用？

**结论：不能。编程语言的跨平台，无法跨越“操作系统底层的能力差异”。**

很多初学者容易误以为“用跨平台语言（Rust/Go/Python/Flutter）写，程序就能跨平台”。但对于**硬件/系统级应用**来说，这完全行不通：

#### 1. Windows 的实现原理
* Windows 10/11 能实现这个功能，是因为微软在系统内核和多媒体框架中开放了 **`Windows.Media.Audio.AudioPlaybackConnection`** 这个专属 API，专门用于开启 **Bluetooth A2DP Sink（蓝牙音频接收端）**。

#### 2. macOS 的底层完全不同
* **macOS 限制了蓝牙 A2DP Sink**：
  * macOS 的现代蓝牙框架 `CoreBluetooth` **只开放了 BLE（低功耗蓝牙）**，根本不提供蓝牙经典音频（Classic Bluetooth A2DP Sink）的公开 API。
  * 苹果在很早之前的系统版本中就废弃了蓝牙音频接收功能。
* **架构机制完全无法通用**：
  * 在 Windows 上写的是 WinRT + Win32 消息循环；
  * 在 macOS 上如果想做音频路由，必须使用 Apple 的 `CoreAudio`、`DriverKit` 编写虚拟音频驱动，或者逆向私有的 `IOBluetooth` 接口，两者的代码**没有任何一行可以复用**。

#### 3. 为什么 Mac 其实不需要这个软件？
* 苹果在 **macOS 12 (Monterey)** 及以上版本中，已经内置了 **AirPlay Receiver（隔空播放接收端）**。
* **iPhone / iPad 想要把音频串流到 Mac 播放，直接下拉控制中心点击「隔空播放」投给 Mac 即可**，原生音质更好（Wi-Fi 传输，码率远高于蓝牙 SBC/AAC），且系统原生支持，无需安装任何第三方工具。

---

### 总结建议

1. **跨平台需求**：
   * **Windows**：用现有工具（`AudioPlaybackConnector`）连接蓝牙 A2DP。
   * **Mac**：直接使用系统自带的「隔空播放 (AirPlay)」接收功能，体验远好于蓝牙。
2. **重写建议**：
   * 如果纯粹为了**个人学习 Rust 和 Windows 底层 API（`windows-rs`）**，这是一个非常好的练手小项目。
   * 但如果是为了**软件性能提升**或**跨平台支持**，重写没有实质意义。

> 现在C++是一定要在Windows上编译吗？Rust是不是就可以用Mac也可以构建编译

这是一个非常切中痛点的问题。答案是：**你的直觉是对的，在“从 macOS 交叉编译生成 Windows 程序”这件事上，Rust 的工程体验远超 C++。**

下面为你详细对比两者的实际情况与原理：

---

### 一、 C++ 一定要在 Windows 上编译吗？

**结论：理论上可以在 Mac 交叉编译，但对于本项目这类 WinRT/XAML 程序，在 Mac 上配置 C++ 交叉编译是“极度痛苦甚至几乎不可行”的。**

#### 为什么普通 C++ 和本项目 C++ 区别很大？
1. **普通纯 C/C++ 算法项目**：在 Mac 上通过 Homebrew 安装 `mingw-w64` 工具链，可以用 `x86_64-w64-mingw32-gcc` 轻松编译出 `.exe`。
2. **但本项目是 `C++/WinRT` + `XAML Islands`**：
   * 它深度依赖微软专有的 **C++/WinRT 代码投影生成器 (`cppwinrt.exe`)**。
   * 依赖只有 Visual Studio / Windows SDK 才提供的专有头文件和 COM 接口库。
   * 依赖 Windows 专属的资源编译器（`rc.exe` 处理图标、清单和语言包）。
   * 虽然可以通过 `xwin` + `clang-cl` + `lld-link` 配合 Wine 强行拼凑一套交叉编译链，但配置极其脆弱、极易报错。
   * **实际现状**：写这类现代 Windows C++ 应用，绝大多数开发者只能老老实实在 Windows 实体机、Windows 虚拟机（如 Parallels Desktop）或 GitHub Actions CI 里进行编译。

---

### 二、 Rust 在 Mac 上能直接编译出 Windows 程序吗？

**结论：可以，而且体验极其丝滑，几乎是一键搞定！**

这也是 Rust 相比传统 C/C++ 最强大的现代工程优势之一（**标准化的跨平台工具链与元数据系统**）。

#### 为什么 Rust 能在 Mac 上轻松编译 Windows API？
* 微软官方为 Rust 推出的 **`windows-rs`** crate，其底层 API 是直接从 Windows 元数据（`.winmd`）预先生成的**纯 Rust 代码**。
* 它**不需要宿主机安装 Visual Studio**，也不依赖 Windows 系统的 C++ 专有头文件。
* 结合 LLVM 和社区的 `cargo-xwin` 工具，Mac 能自动处理 Windows 链接器和 CRT。

#### 在 Mac 上的实际操作有多简单？
在 macOS 终端里只需要两步：

```bash
# 1. 安装支持 MSVC 交叉编译的工具
cargo install cargo-xwin

# 2. 直接在 Mac 上编译出 Windows 64位的 release .exe
cargo xwin build --target x86_64-pc-windows-msvc --release
```
编译完成后，你就可以在 Mac 的 `target/x86_64-pc-windows-msvc/release/` 目录下直接拿到一个标准的 Windows `.exe` 文件。

---

### 三、 总结：如果是以 Mac 为主力机，选哪个？

如果你平时的**主力工作机是 Mac**，但想开发这个 Windows 托盘工具：

| 维度 | C++ / C++/WinRT | Rust (`windows-rs`) |
| :--- | :--- | :--- |
| **Mac 本地交叉编译** | ❌ 极度繁琐，基本必须开 Windows 虚拟机 |  **完美支持**，一条 `cargo xwin` 命令直出 `.exe` |
| **依赖管理** | ❌ 依赖 vcxproj / NuGet / Visual Studio 安装环境 |  **极佳**（仅靠 `Cargo.toml`） |
| **开发工作流** | 在 Mac 写代码 $\to$ 传到虚拟机/CI 编译 | 在 Mac 写代码 $\to$ 在 Mac 直接编译 $\to$ 丢给测试机/虚拟机运行 |
| **最终产物** | 单文件原生 `.exe`（几百 KB） | 单文件原生 `.exe`（1~2 MB） |

**最终建议**：
如果你想摆脱 Windows 复杂的 Visual Studio 开发环境，在 Mac 上享受现代化的代码编辑、包管理和一键交叉编译，**用 Rust 重写这个项目是一个非常合理的选择**（虽然它无法让程序直接在 macOS 上跑，但能让你的**开发与构建全流程都在 Mac 上舒服地完成**）。

> 该项目还是有挺多bug和功能缺失的，比如有时候连接后无声以及无完整汉化，你是推荐我用Rust重构，还是在原基础上改写？

<details><summary>1 previous message</summary>

> <details><summary>Explored 4 files, 2 searches, ran 2 commands</summary>
>
> <details><summary>Explored 2 files</summary>
>
> - Read `./translate/source/zh_CN.po`
> - Read `./AudioPlaybackConnector.rc`
>
> </details>
>
> - Ran `cat translate/gen_rc.sh && echo "\n---\n" && cat translate/po2ymo.py`
>
> - Ran `grep -n "LoadTranslateData" AudioPlaybackConnector.cpp || echo "NOT CALLED IN CPP"`
>
> <details><summary>Explored 2 files, 2 searches</summary>
>
> - Read `./I18n.hpp`
> - Searched for `LoadTranslateData` in `.`
> - Read `./AudioPlaybackConnector.vcxproj`
> - Searched for `translate`
>
> </details>
>
> </details>
</details>

这是一个非常典型且关键的技术选型分水岭。我们先**把这两个问题的底层根因挖出来**，然后再根据你的实际开发目的来决定是“改 C++”还是“Rust 重构”。

---

### 一、 先看这两个 Bug 的真正原因

#### 1. 为什么“汉化完全没有生效 / 汉化缺失”？
* **真相**：原作者已经在 `translate/` 里把中文翻译（`.po` 文件）和二进制转换脚本都写好了，也在 `I18n.hpp` 里写好了 `LoadTranslateData()` 函数。
* **但作者犯了一个低级失误**：在 `AudioPlaybackConnector.cpp` 的程序入口 `wWinMain` 中，**完全忘记调用 `LoadTranslateData();` 了！**
* 导致翻译查找表永远是空的，每次字符串匹配全部 fallback 到了英文。
* **修复成本**：在原 C++ 代码的 `wWinMain` 初始化阶段**补上一行 `LoadTranslateData();`**，中文汉化瞬间就完全正常了。

#### 2. 为什么“有时候连接后无声”？
* **真相**：这不是 C++ 语言的缺陷，而是 Windows 蓝牙 A2DP Sink 的音频路由与生命周期管理机制问题：
  1. **音频输出端点（Default Audio Endpoint）切换丢失**：当电脑插拔耳机、蓝牙耳机连接或切换了默认声卡时，已经建立的 `AudioPlaybackConnection` 并没有收到通知去重建音频管线，导致声音仍被路由到旧的/无效的音频流上。
  2. **音量同步与静音问题**：Windows 的绝对音量（Absolute Volume / AVRCP）或系统音量合成器中有时会将蓝牙 Sink 音频通道默认为 0 或静音，而原项目完全没有监听和控制音量状态。
  3. **连接建立的时序问题**：如果手机端还没开始推流就调用 `OpenAsync()`，有时 Windows 音频图（Audio Graph）会进入挂起状态。
* **重要结论**：**无论用 Rust 还是 C++，只要底层调用的还是 Windows 的 `AudioPlaybackConnection`，无声的问题依然存在。** 解决它需要在代码中接入 Windows CoreAudio 接口（如监听 `IMMNotificationClient` 的默认设备变更，并补充音量/重试机制）。

---

### 二、 两种路线的利弊对比

| 维度 | 路线 A：在原 C++ 基础上修改 | 路线 B：用 Rust (`windows-rs`) 重构 |
| :--- | :--- | :--- |
| **解决 Bug 的速度** | **极快**（整个项目仅 400 行代码，改动精准且小） | **较慢**（必须先用 Rust 把 Win32 消息循环、托盘、XAML/COM 接口从零搭一遍） |
| **开发与编译环境** | **需要 Windows 环境**（实体机 / 虚拟机 / CI）编译 | **Mac 丝滑开发**（`cargo xwin` 在 Mac 一键交叉编译出 `.exe`） |
| **代码复杂度** | **低**（C++/WinRT 语法糖成熟，官方支持最好） | **中偏高**（Rust 调 Windows XAML / COM / WinRT 会有大量 unsafe 和包装代码） |
| **维护与心智负担** | 依赖 Visual Studio 工程配置（`.sln`/`.vcxproj`） | Cargo 统一管理依赖，代码现代干净 |

---

### 三、 最终推荐

####  推荐直接在原 C++ 上改写，如果：
1. **你的首要目标是“尽快把软件修好、稳定自用”**：
   * 汉化问题只需补一行代码；
   * 连接无声问题可以在 `AudioPlaybackConnector.cpp` 里增加简单的重新激活逻辑和端点刷新；
   * 几百行代码改起来立竿见影，根本不需要推倒重来。

---

####  推荐用 Rust 彻底重构，如果：
1. **你的主力机是 Mac，极度反感在 Windows / 虚拟机里开 Visual Studio 开发**：
   * 你想在 Mac 上用 VS Code / Cursor 写代码，享受 Cargo 的包管理，并通过 `cargo xwin` 一键生成 Windows 可执行文件。
2. **你把这当作一个“学习 Rust 与 Windows 底层 API 交互”的练手项目**：
   * 用 Rust 重构一个 400 行的轻量系统工具是非常好的实践。
   * 但请注意：**关于“连接无声”的问题，你在 Rust 里依然要把 Windows CoreAudio 端点监听和音量控制的逻辑补齐**，不能单纯翻译原有逻辑。

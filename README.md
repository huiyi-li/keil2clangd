# Keil2Json

[English](#english) | [中文](#中文)

Keil2Json generates `compile_commands.json` for embedded projects that use Keil MDK, IAR EWARM, or Makefile-based builds. The generated database can be used by clangd, VS Code C/C++, and other language servers for code navigation, completion, and diagnostics.

Keil2Json 可以从 Keil MDK、IAR EWARM 和 Makefile 工程生成 `compile_commands.json`，用于 clangd、VS Code C/C++ 等工具的代码跳转、补全和诊断。

## English

### Features

- Keil MDK project support: parses `.uvprojx` files.
- IAR EWARM project support: parses `.ewp` files.
- Makefile project support: runs `make clean`, `make -n`, and `make` to capture compile commands.
- Automatic CMSIS and C library include path injection.
- Persistent setup wizard for Keil/IAR/CMSIS configuration.
- Keil target selection for multi-target `.uvprojx` projects.
- Project type selection when Keil, IAR, and Makefile projects exist in the same directory.
- Optional Keil UV4 build, clean, flash, download, and debug actions.
- Python executable for full compatibility and C++ executable for smaller size and faster startup.

### Supported Projects

| Project type | File | Behavior |
| --- | --- | --- |
| Keil MDK | `.uvprojx` | Parses sources, include paths, defines, selected target, and ARMCC/ARMCLANG include paths. |
| IAR EWARM | `.ewp` | Parses sources, include paths, defines, CMSIS include, and IAR C library include. |
| Makefile | `Makefile` or `makefile` | Runs Make and extracts compiler commands from `make -n`. |

### Quick Start

Run in a project directory:

```powershell
Keil2Json.exe
Keil2JsonCpp.exe
```

Specify a project directory or project file:

```powershell
Keil2Json.exe -p D:\Project\Demo
Keil2JsonCpp.exe -p D:\Project\Demo
```

Generate absolute paths:

```powershell
Keil2Json.exe -p D:\Project\Demo --absolute
Keil2JsonCpp.exe -p D:\Project\Demo --absolute
```

### First-Time Setup

Run the setup wizard before first use:

```powershell
Keil2Json.exe --setup
Keil2JsonCpp.exe --setup
```

The wizard scans the Windows registry for Keil and IAR installations, then asks you to select or manually enter CMSIS and C library include paths.

Configuration is stored permanently:

| Platform | Path |
| --- | --- |
| Windows | `%APPDATA%\KeilFormat\config.json` |
| Linux | `~/.config/KeilFormat/config.json` |

Show the current configuration:

```powershell
Keil2Json.exe --show-config
Keil2JsonCpp.exe --show-config
```

### CMSIS and Toolchain Includes

For Keil, the tool reads `TOOLS.INI` and uses `RTEPATH` when available. For example:

```ini
RTEPATH="D:\keil\Keil_v5\Arm\Packs"
```

The corresponding CMSIS package root is resolved as:

```text
D:\keil\Keil_v5\Arm\Packs\ARM\CMSIS
```

The setup also supports legacy Keil 4 style CMSIS paths, such as:

```text
D:\keil\Keil_v4\ARM\CMSIS\Include
```

Keil compiler include paths are injected according to the selected project compiler:

- ARMCC: `ARMCC\include`
- ARMCLANG: `ARMCLANG\include`

For IAR, common paths are:

```text
<IAR>\arm\CMSIS
<IAR>\arm\inc\c
```

If no Keil or IAR installation is detected, the setup wizard asks for manual CMSIS and C library include paths.

### CLI Options

| Option | Description |
| --- | --- |
| `-p, --path <path>` | Project directory or project file. Defaults to the current directory. |
| `-a, --absolute` | Write absolute paths in `compile_commands.json`. |
| `--project-type <type>` | Select `keil`, `iar`, `makefile`, or `make`. |
| `-s, --setup` | Run the persistent setup wizard. |
| `--show-config` | Print the saved configuration. |
| `-n, --dry-run` | For Makefile projects, run `make clean` and `make -n` only. |
| `--keil_build` | Run a Keil UV4 action instead of generating `compile_commands.json`. |
| `--keil_action <action>` | `build`, `rebuild`, `clean`, `flash`, `download`, or `debug`. |
| `-t, --target <name>` | Keil target name for JSON generation or Keil UV4 actions. |
| `--list-targets` | List Keil targets and exit. |
| `--keil_uv4 <path>` | Override the `UV4.exe` path. |
| `--keil_jobs <n>` | Keil UV4 `-j` value when the Keil window is hidden. Debug never uses `-j`. |
| `--keil_log <path>` | Keil UV4 output log path. |
| `--keil_window` | Show the Keil window. Debug always shows the window. |
| `-h, --help` | Show help. |

### Project Selection

If a directory contains more than one project type, the tool asks which branch to use:

- Keil: parse `.uvprojx` and generate JSON.
- IAR: parse `.ewp` and generate JSON.
- Makefile: run Make and generate JSON from captured compile commands.

Use `--project-type` to skip the prompt:

```powershell
Keil2Json.exe -p . --project-type keil
Keil2Json.exe -p . --project-type iar
Keil2Json.exe -p . --project-type makefile
```

If multiple Keil or IAR project files exist, the tool asks you to select one. Makefile projects are directory-based: if both `Makefile` and `makefile` exist in the same directory, the tool does not ask which file to use; it runs the default Make behavior in that directory.

### Keil Target Selection

If a Keil project has one target, that target is used automatically. If it has multiple targets, the tool asks you to select one.

You can also specify it directly:

```powershell
Keil2Json.exe -p . --project-type keil --target "Target 1"
Keil2JsonCpp.exe -p . --project-type keil --target "Target 1"
```

### Keil UV4 Actions

List targets:

```powershell
Keil2Json.exe -p . --list-targets
Keil2JsonCpp.exe -p . --list-targets
```

Build a target:

```powershell
Keil2Json.exe -p . --keil_build --keil_action build -t "Target 1"
Keil2JsonCpp.exe -p . --keil_build --keil_action build -t "Target 1"
```

Other actions:

```powershell
Keil2Json.exe -p . --keil_build --keil_action clean
Keil2Json.exe -p . --keil_build --keil_action rebuild
Keil2Json.exe -p . --keil_build --keil_action flash
Keil2Json.exe -p . --keil_build --keil_action debug
```

Keil window behavior:

- Non-debug actions pass `-j` by default, so the Keil window is hidden.
- `--keil_window` disables `-j` and shows the Keil window.
- `debug` always shows the Keil window and never passes `-j`.
- `--keil_jobs` controls the `-j` value when the window is hidden.

Keil log behavior:

- `build` writes to `build_log` by default.
- `clean`, `rebuild`, `flash`, and `debug` write to `Prg_Output` by default.
- Use `--keil_log <path>` to override the log path.
- Both Python and C++ versions stream Keil build logs while UV4 is running.

### Output Path Rules

The generated file is always named:

```text
compile_commands.json
```

Default output location:

| Selected branch | Output directory |
| --- | --- |
| Keil | Directory containing the selected `.uvprojx`. |
| IAR | Directory containing the selected `.ewp`. |
| Makefile | The Makefile project directory. |

Examples:

```text
D:\Project\App\App.uvprojx      -> D:\Project\App\compile_commands.json
D:\Project\App\App.ewp          -> D:\Project\App\compile_commands.json
D:\Project\App\Makefile         -> D:\Project\App\compile_commands.json
```

### Install from Release

Download the archive for your platform from the GitHub Releases page and extract it.

Recommended Windows usage:

```powershell
Keil2Json.exe -p .
Keil2JsonCpp.exe -p .
```

If an old executable is still being used, check `PATH`:

```powershell
where Keil2Json.exe
where Keil2JsonCpp.exe
```

### Build from Source

Run the Python version directly:

```powershell
python Keil2Json.py -p .
```

Build the Python executable:

```powershell
python -m PyInstaller --clean --noconfirm --onefile --console --name Keil2Json --distpath dist Keil2Json.py
```

Build the C++ executable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cpp\build.ps1
```

The C++ executable is generated at:

```text
dist-cpp\Keil2JsonCpp.exe
```

## 中文

### 功能特性

- 支持 Keil MDK 工程：解析 `.uvprojx`。
- 支持 IAR EWARM 工程：解析 `.ewp`。
- 支持 Makefile 工程：执行 `make clean`、`make -n` 和 `make` 捕获编译命令。
- 自动补充 CMSIS 和 C 库 include 路径。
- 提供持久化配置向导，用于保存 Keil/IAR/CMSIS 配置。
- 支持 Keil 多 Target 工程选择。
- 当同一目录存在 Keil、IAR、Makefile 多种工程时，可交互选择或通过参数指定分支。
- 支持调用 Keil UV4 执行 build、clean、flash、download 和 debug。
- 提供 Python 完整功能版和 C++ 小体积快速启动版。

### 支持的工程

| 工程类型 | 文件 | 行为 |
| --- | --- | --- |
| Keil MDK | `.uvprojx` | 解析源文件、include、宏定义、选中的 Target，以及 ARMCC/ARMCLANG include。 |
| IAR EWARM | `.ewp` | 解析源文件、include、宏定义、CMSIS include 和 IAR C 库 include。 |
| Makefile | `Makefile` 或 `makefile` | 执行 Make，并从 `make -n` 输出中提取编译命令。 |

### 快速开始

在工程目录运行：

```powershell
Keil2Json.exe
Keil2JsonCpp.exe
```

指定工程目录或工程文件：

```powershell
Keil2Json.exe -p D:\Project\Demo
Keil2JsonCpp.exe -p D:\Project\Demo
```

生成绝对路径：

```powershell
Keil2Json.exe -p D:\Project\Demo --absolute
Keil2JsonCpp.exe -p D:\Project\Demo --absolute
```

### 首次配置

首次使用建议运行配置向导：

```powershell
Keil2Json.exe --setup
Keil2JsonCpp.exe --setup
```

配置向导会扫描 Windows 注册表中的 Keil 和 IAR 安装路径，并引导选择或手动输入 CMSIS 和 C 库 include 路径。

配置会长期保存：

| 平台 | 路径 |
| --- | --- |
| Windows | `%APPDATA%\KeilFormat\config.json` |
| Linux | `~/.config/KeilFormat/config.json` |

查看当前配置：

```powershell
Keil2Json.exe --show-config
Keil2JsonCpp.exe --show-config
```

### CMSIS 和工具链 include

Keil 会优先读取 `TOOLS.INI` 中的 `RTEPATH`。例如：

```ini
RTEPATH="D:\keil\Keil_v5\Arm\Packs"
```

对应 CMSIS 包路径会按下面的形式推导：

```text
D:\keil\Keil_v5\Arm\Packs\ARM\CMSIS
```

同时兼容 Keil 4 风格路径：

```text
D:\keil\Keil_v4\ARM\CMSIS\Include
```

Keil 工具链 include 会根据工程编译器自动补充：

- ARMCC：`ARMCC\include`
- ARMCLANG：`ARMCLANG\include`

IAR 常见路径：

```text
<IAR>\arm\CMSIS
<IAR>\arm\inc\c
```

如果没有扫描到 Keil 或 IAR，配置向导会提示手动输入 CMSIS 和 C 库 include 路径。

### 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-p, --path <path>` | 工程目录或工程文件。默认当前目录。 |
| `-a, --absolute` | 在 `compile_commands.json` 中写入绝对路径。 |
| `--project-type <type>` | 指定 `keil`、`iar`、`makefile` 或 `make` 分支。 |
| `-s, --setup` | 运行持久化配置向导。 |
| `--show-config` | 打印已保存配置。 |
| `-n, --dry-run` | Makefile 工程只执行 `make clean` 和 `make -n`。 |
| `--keil_build` | 调用 Keil UV4 操作，不生成 `compile_commands.json`。 |
| `--keil_action <action>` | `build`、`rebuild`、`clean`、`flash`、`download` 或 `debug`。 |
| `-t, --target <name>` | Keil Target 名称，用于 JSON 生成或 UV4 操作。 |
| `--list-targets` | 列出 Keil Target 后退出。 |
| `--keil_uv4 <path>` | 手动指定 `UV4.exe` 路径。 |
| `--keil_jobs <n>` | Keil 窗口隐藏时使用的 UV4 `-j` 参数。debug 不使用 `-j`。 |
| `--keil_log <path>` | 指定 Keil UV4 日志输出路径。 |
| `--keil_window` | 显示 Keil 窗口。debug 总是显示窗口。 |
| `-h, --help` | 显示帮助信息。 |

### 工程分支选择

如果同一目录存在多种工程类型，工具会提示选择分支：

- Keil：解析 `.uvprojx` 并生成 JSON。
- IAR：解析 `.ewp` 并生成 JSON。
- Makefile：执行 Make 并根据捕获的编译命令生成 JSON。

使用 `--project-type` 可以跳过交互：

```powershell
Keil2Json.exe -p . --project-type keil
Keil2Json.exe -p . --project-type iar
Keil2Json.exe -p . --project-type makefile
```

如果存在多个 Keil 或 IAR 工程文件，工具会继续提示选择具体工程。Makefile 按目录处理：即使同一目录同时存在 `Makefile` 和 `makefile`，也不会展示 Makefile 文件选择，而是按该目录默认 Make 行为执行。

### Keil Target 选择

如果 Keil 工程只有一个 Target，会自动使用该 Target。如果存在多个 Target，工具会提示选择。

也可以直接指定：

```powershell
Keil2Json.exe -p . --project-type keil --target "Target 1"
Keil2JsonCpp.exe -p . --project-type keil --target "Target 1"
```

### Keil UV4 操作

列出 Target：

```powershell
Keil2Json.exe -p . --list-targets
Keil2JsonCpp.exe -p . --list-targets
```

构建指定 Target：

```powershell
Keil2Json.exe -p . --keil_build --keil_action build -t "Target 1"
Keil2JsonCpp.exe -p . --keil_build --keil_action build -t "Target 1"
```

其他操作：

```powershell
Keil2Json.exe -p . --keil_build --keil_action clean
Keil2Json.exe -p . --keil_build --keil_action rebuild
Keil2Json.exe -p . --keil_build --keil_action flash
Keil2Json.exe -p . --keil_build --keil_action debug
```

Keil 窗口行为：

- 默认情况下，非 debug 操作会传入 `-j`，Keil 窗口隐藏。
- 使用 `--keil_window` 时不传 `-j`，Keil 窗口显示。
- `debug` 总是显示 Keil 窗口，并且永远不传 `-j`。
- `--keil_jobs` 用于设置隐藏窗口时的 `-j` 数值。

Keil 日志行为：

- `build` 默认写入 `build_log`。
- `clean`、`rebuild`、`flash`、`debug` 默认写入 `Prg_Output`。
- 可通过 `--keil_log <path>` 指定日志路径。
- Python 版和 C++ 版都会在 UV4 运行期间实时输出 Keil 构建日志。

### 输出路径规则

生成文件名固定为：

```text
compile_commands.json
```

默认输出位置：

| 选择的分支 | 输出目录 |
| --- | --- |
| Keil | 选中的 `.uvprojx` 所在目录。 |
| IAR | 选中的 `.ewp` 所在目录。 |
| Makefile | Makefile 工程目录。 |

示例：

```text
D:\Project\App\App.uvprojx      -> D:\Project\App\compile_commands.json
D:\Project\App\App.ewp          -> D:\Project\App\compile_commands.json
D:\Project\App\Makefile         -> D:\Project\App\compile_commands.json
```

### 从 Release 使用

从 GitHub Releases 下载对应平台压缩包，解压后直接运行：

```powershell
Keil2Json.exe -p .
Keil2JsonCpp.exe -p .
```

如果运行到旧版本，检查 `PATH`：

```powershell
where Keil2Json.exe
where Keil2JsonCpp.exe
```

### 从源码构建

直接运行 Python 版：

```powershell
python Keil2Json.py -p .
```

打包 Python exe：

```powershell
python -m PyInstaller --clean --noconfirm --onefile --console --name Keil2Json --distpath dist Keil2Json.py
```

构建 C++ exe：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cpp\build.ps1
```

C++ 产物路径：

```text
dist-cpp\Keil2JsonCpp.exe
```

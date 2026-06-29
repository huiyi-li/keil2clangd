# Keil2Json

Keil2Json 用于从 Keil MDK、IAR EWARM 和 Makefile 工程生成 `compile_commands.json`，供 clangd、VS Code C/C++ 插件等工具进行代码跳转、补全和诊断。

当前提供两个可执行版本：

- `Keil2Json.exe`：Python 版打包产物，功能完整。
- `Keil2JsonCpp.exe`：C++ 版实现，体积更小、启动更快。

## 支持的工程

- Keil MDK：扫描 `.uvprojx`。
- IAR EWARM：扫描 `.ewp`。
- Makefile：扫描 `Makefile` 或 `makefile`，通过 `make clean`、`make -n`、`make` 捕获编译命令。

生成结果会写入工程目录下的 `compile_commands.json`。

## 快速使用

在工程目录下运行：

```powershell
Keil2Json.exe
```

或使用 C++ 版：

```powershell
Keil2JsonCpp.exe
```

指定工程路径：

```powershell
Keil2Json.exe -p D:\Project\Demo
Keil2JsonCpp.exe -p D:\Project\Demo
```

生成绝对路径形式的 `compile_commands.json`：

```powershell
Keil2Json.exe -p D:\Project\Demo -a
Keil2JsonCpp.exe -p D:\Project\Demo -a
```

## 首次配置

首次使用建议先执行配置向导：

```powershell
Keil2Json.exe --setup
```

或：

```powershell
Keil2JsonCpp.exe --setup
```

短参数也可以使用：

```powershell
Keil2Json.exe -s
Keil2JsonCpp.exe -s
```

配置向导会尝试从 Windows 注册表扫描 Keil 和 IAR 的安装路径，并引导选择 CMSIS 版本或手动输入 CMSIS include 路径。

### Keil 配置内容

Keil 配置会记录：

- Keil 安装目录。
- CMSIS include 路径，例如 `C:\Keil_v5\ARM\CMSIS\5.9.0\CMSIS\Core\Include`。
- ARMCC include 路径。
- ARMCLANG include 路径。

Keil 的 CMSIS 路径优先根据 `TOOLS.INI` 中的 `RTEPATH` 推导。例如：

```ini
RTEPATH="D:\keil\Keil_v5\Arm\Packs"
```

对应 CMSIS 包路径会按 `D:\keil\Keil_v5\Arm\Packs\ARM\CMSIS` 查找。

### IAR 配置内容

IAR 配置会记录：

- IAR 安装目录。
- CMSIS include 路径，通常来自 `IAR安装目录\arm\CMSIS`。
- IAR C 库 include 路径，通常为 `IAR安装目录\arm\inc\c`。

如果注册表没有扫描到 Keil 或 IAR，配置向导会提示手动输入对应路径；不需要的工具链可以直接跳过。

## 配置保存位置

配置会长期保存，不需要每个工程重复设置。

Windows：

```text
%APPDATA%\KeilFormat\config.json
```

Linux：

```text
~/.config/KeilFormat/config.json
```

查看当前配置：

```powershell
Keil2Json.exe --show-config
Keil2JsonCpp.exe --show-config
```

重新配置时再次执行 `--setup` 即可覆盖旧配置。

## 参数说明

```text
-p, --path <path>    指定工程目录，默认是当前目录。
-a, --absolute       在 compile_commands.json 中输出绝对路径。
-s, --setup          运行配置向导，扫描并保存 Keil/IAR/CMSIS 配置。
--show-config        打印当前持久化配置。
--keil_build         调用 Keil UV4 执行构建、清理、下载或调试。
--keil_action        Keil 操作，可选 build、rebuild、clean、flash、download、debug。
-t, --target         指定 Keil Target 名称。
--list-targets       列出 Keil 工程中的 Target。
--keil_uv4           手动指定 UV4.exe 路径。
--keil_jobs          Keil UV4 -j 参数，仅在隐藏 Keil 窗口时使用；debug 不使用 -j。
--keil_log           指定 Keil UV4 输出日志路径。
--keil_window        显示 Keil 窗口；debug 总是显示窗口。
-h, --help           显示帮助信息。
```

示例：

```powershell
Keil2Json.exe -p .
Keil2Json.exe --path D:\Project\Demo --absolute
Keil2Json.exe --setup
Keil2Json.exe --show-config
Keil2Json.exe -p . --list-targets
Keil2Json.exe -p . --keil_build --keil_action build -t "Target 1"
```

C++ 版参数保持一致：

```powershell
Keil2JsonCpp.exe -p .
Keil2JsonCpp.exe --path D:\Project\Demo --absolute
Keil2JsonCpp.exe --setup
Keil2JsonCpp.exe --show-config
Keil2JsonCpp.exe -p . --list-targets
Keil2JsonCpp.exe -p . --keil_build --keil_action build -t "Target 1"
```

## Keil 工程生成流程

运行工具后会递归查找 `.uvprojx` 文件。

生成时会读取工程中的：

- 源文件列表。
- include 路径。
- 宏定义。
- 当前使用的 ARMCC 或 ARMCLANG 信息。

工具会根据配置自动补充：

- 已选择的 CMSIS include 路径。
- ARMCC 工程补充 `ARMCC\include`。
- ARMCLANG 工程补充 `ARMCLANG\include`。

## Keil UV4 操作

除生成 `compile_commands.json` 外，工具也可以直接调用 Keil 安装目录下的 `UV4.exe` 执行工程操作。该功能仅支持 Windows。

列出工程 Target：

```powershell
Keil2Json.exe -p . --list-targets
Keil2JsonCpp.exe -p . --list-targets
```

构建指定 Target：

```powershell
Keil2Json.exe -p . --keil_build --keil_action build -t "Target 1"
Keil2JsonCpp.exe -p . --keil_build --keil_action build -t "Target 1"
```

清理、重建、下载和调试：

```powershell
Keil2Json.exe -p . --keil_build --keil_action clean
Keil2Json.exe -p . --keil_build --keil_action rebuild
Keil2Json.exe -p . --keil_build --keil_action flash
Keil2Json.exe -p . --keil_build --keil_action debug
```

C++ 版参数相同：

```powershell
Keil2JsonCpp.exe -p . --keil_build --keil_action clean
Keil2JsonCpp.exe -p . --keil_build --keil_action rebuild
Keil2JsonCpp.exe -p . --keil_build --keil_action flash
Keil2JsonCpp.exe -p . --keil_build --keil_action debug
```

`UV4.exe` 查找顺序：

- `--keil_uv4` 指定的路径。
- 配置文件中的 Keil 安装目录推导出的 `UV4\UV4.exe`。
- 常见默认路径，例如 `C:\Keil_v5\UV4\UV4.exe`。
- PATH 环境变量。

窗口行为：

- 默认情况下，非 debug 操作会传入 `-j`，让 Keil 不弹出窗口。
- 使用 `--keil_window` 时不传 `-j`，Keil 窗口会显示。
- `debug` 操作必须弹出 Keil 窗口，因此永远不传 `-j`。
- `--keil_jobs` 只在隐藏 Keil 窗口时作为 `-j` 的数值使用，默认 build 为 `-j16`，其他操作为 `-j0`。

日志输出：

- build 默认写入工程目录下的 `build_log`。
- clean、rebuild、flash、debug 默认写入工程目录下的 `Prg_Output`。
- 可以通过 `--keil_log <path>` 指定日志路径。

## IAR 工程生成流程

运行工具后会递归查找 `.ewp` 文件。

生成时会读取工程中的：

- 源文件列表。
- include 路径。
- 宏定义。

工具会根据配置自动补充：

- IAR CMSIS include 路径。
- IAR C 库 include 路径，例如 `arm\inc\c`。

## Makefile 工程生成流程

运行工具后如果检测到 `Makefile` 或 `makefile`，会按以下顺序执行：

```powershell
make clean
make -n
make
```

其中 `make -n` 用于捕获实际编译命令，`make` 用于执行真实构建。工具会从输出中提取 `gcc`、`g++`、`clang`、`arm-none-eabi-gcc` 等编译命令并生成 `compile_commands.json`。

如果工程的 Makefile 需要特定 target，请先确认默认 target 可以完整构建。

## Release exe 使用方式

从 Release 页面下载对应平台的压缩包，解压后可以直接运行。

推荐将 exe 放到 PATH 目录中，例如 Windows 下：

```text
C:\MinGW\bin
```

之后可以在任意工程目录直接运行：

```powershell
Keil2Json.exe -p .
```

如果命令行运行的行为和刚下载的 exe 不一致，先检查 PATH 中是否存在旧版本：

```powershell
where Keil2Json.exe
where Keil2JsonCpp.exe
```

优先使用明确路径运行可以避免误用旧版本：

```powershell
D:\Tools\Keil2Json.exe -p .
D:\Tools\Keil2JsonCpp.exe -p .
```

## 从源码运行

Python 版：

```powershell
python Keil2Json.py -p .
```

打包 Python exe：

```powershell
python -m PyInstaller --clean --noconfirm --onefile --console --name Keil2Json --distpath dist Keil2Json.py
```

C++ 版：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cpp\build.ps1
```

生成文件位于：

```text
dist-cpp\Keil2JsonCpp.exe
```

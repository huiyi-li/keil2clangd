#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import configparser
import json
import os
import re
import shlex
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

IS_WINDOWS = sys.platform == "win32"
TOOL_NAME = "KeilFormat"

if IS_WINDOWS:
    import winreg


def config_dir():
    if IS_WINDOWS:
        return Path(os.environ.get("APPDATA", Path.home())) / TOOL_NAME
    return Path.home() / ".config" / TOOL_NAME


def config_path():
    return config_dir() / "config.json"


class ConfigManager:
    DEFAULT_CONFIG = {
        "version": 2,
        "keil": {
            "install_path": "",
            "cmsis_path": "",
            "armcc_include": "",
            "armclang_include": "",
        },
        "iar": {
            "install_path": "",
            "cmsis_path": "",
            "c_include": "",
            "arm_install_path": "",
            "arm_cmsis_path": "",
            "arm_c_include": "",
            "rl78_install_path": "",
            "rl78_c_include": "",
            "rl78_inc": "",
        },
    }

    def __init__(self):
        self.path = config_path()
        self.config = self.load()

    def exists(self):
        return self.path.exists()

    def load(self):
        cfg = json.loads(json.dumps(self.DEFAULT_CONFIG))
        if not self.path.exists():
            return cfg
        try:
            with self.path.open("r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError):
            return cfg
        for key, value in data.items():
            if isinstance(value, dict) and isinstance(cfg.get(key), dict):
                cfg[key].update(value)
            else:
                cfg[key] = value
        return cfg

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("w", encoding="utf-8") as f:
            json.dump(self.config, f, indent=4, ensure_ascii=False)

    def get(self, section, key):
        if section == "iar":
            iar = self.config.get("iar", {})
            if key == "arm_install_path":
                val = iar.get("arm_install_path") or iar.get("install_path", "")
                return val if val else None
            if key == "arm_cmsis_path":
                val = iar.get("arm_cmsis_path") or iar.get("cmsis_path", "")
                return val if val else None
            if key == "arm_c_include":
                val = iar.get("arm_c_include") or iar.get("c_include", "")
                return val if val else None
        value = self.config.get(section, {}).get(key, "")
        return value if value else None


class RegistryScanner:
    @staticmethod
    def read_value(root, subkey, value_name, access):
        try:
            with winreg.OpenKey(root, subkey, 0, access) as key:
                value, _ = winreg.QueryValueEx(key, value_name)
                return value
        except OSError:
            return None

    @staticmethod
    def enum_subkeys(root, subkey, access):
        try:
            with winreg.OpenKey(root, subkey, 0, access) as key:
                index = 0
                while True:
                    try:
                        yield winreg.EnumKey(key, index)
                        index += 1
                    except OSError:
                        break
        except OSError:
            return

    @staticmethod
    def registry_views():
        views = [winreg.KEY_READ]
        if hasattr(winreg, "KEY_WOW64_32KEY"):
            views.append(winreg.KEY_READ | winreg.KEY_WOW64_32KEY)
        if hasattr(winreg, "KEY_WOW64_64KEY"):
            views.append(winreg.KEY_READ | winreg.KEY_WOW64_64KEY)
        return views

    @classmethod
    def find_keil(cls):
        if not IS_WINDOWS:
            return []
        found = set()
        roots = [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]
        for root in roots:
            for access in cls.registry_views():
                for base in (r"SOFTWARE\Keil\Products",):
                    for product in cls.enum_subkeys(root, base, access):
                        path = cls.read_value(root, rf"{base}\{product}", "PATH", access)
                        path = normalize_install_path(path, "keil")
                        if path:
                            found.add(path)
                for base in (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",):
                    for subkey in cls.enum_subkeys(root, base, access):
                        key = rf"{base}\{subkey}"
                        name = cls.read_value(root, key, "DisplayName", access)
                        if name and "keil" in str(name).lower():
                            path = cls.read_value(root, key, "InstallLocation", access)
                            path = normalize_install_path(path, "keil")
                            if path:
                                found.add(path)
        return sorted(found, key=len)

    @classmethod
    def find_iar(cls):
        if not IS_WINDOWS:
            return []
        found = set()
        roots = [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]
        for root in roots:
            for access in cls.registry_views():
                for base in (r"SOFTWARE\IAR Systems\Embedded Workbench",):
                    for version in cls.enum_subkeys(root, base, access):
                        key = rf"{base}\{version}"
                        for value_name in ("InstallPath", "InstallLocation"):
                            path = cls.read_value(root, key, value_name, access)
                            path = normalize_install_path(path, "iar")
                            if path:
                                found.add(path)
                for base in (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",):
                    for subkey in cls.enum_subkeys(root, base, access):
                        key = rf"{base}\{subkey}"
                        name = cls.read_value(root, key, "DisplayName", access)
                        if name and "iar" in str(name).lower() and "embedded" in str(name).lower():
                            path = cls.read_value(root, key, "InstallLocation", access)
                            path = normalize_install_path(path, "iar")
                            if path:
                                found.add(path)
        return sorted(found, key=len)

    @classmethod
    def find_iar_by_arch(cls, arch="arm"):
        found = []
        for p_str in cls.find_iar():
            p = Path(p_str)
            if arch == "arm":
                if (p / "arm").is_dir() or (p / "arm" / "inc" / "c").is_dir():
                    found.append(p_str)
            elif arch == "rl78":
                if (p / "rl78").is_dir() or (p / "rl78" / "inc" / "c").is_dir():
                    found.append(p_str)
        return found


def normalize_install_path(value, tool):
    if not value:
        return None
    path = Path(str(value).strip().strip('"')).expanduser()
    if not path.exists() or not path.is_dir():
        return None

    path = path.resolve()
    leaf = path.name.lower()

    # Registry values may point to C:\Keil_v5\ARM or C:\IAR...\arm or C:\IAR...\rl78.
    # Keep install_path as the IDE root so later path derivation does not add ARM/arm twice.
    if tool == "keil" and leaf == "arm":
        return str(path.parent)
    if tool == "iar" and leaf in {"arm", "rl78"}:
        return str(path.parent)

    # Some registry values point one level below the root, for example a product/toolchain folder.
    if tool == "keil":
        if (path / "ARM" / "CMSIS").is_dir() or (path / "TOOLS.INI").exists():
            return str(path)
        if (path.parent / "ARM" / "CMSIS").is_dir() or (path.parent / "TOOLS.INI").exists():
            return str(path.parent.resolve())
    elif tool == "iar":
        if (path / "arm" / "CMSIS").is_dir() or (path / "arm" / "inc" / "c").is_dir() or (path / "rl78" / "inc" / "c").is_dir():
            return str(path)
        if (path.parent / "arm" / "CMSIS").is_dir() or (path.parent / "arm" / "inc" / "c").is_dir() or (path.parent / "rl78" / "inc" / "c").is_dir():
            return str(path.parent.resolve())

    return str(path)


def ide_subdir_or_root(install_path, subdir_name):
    root = Path(install_path)
    if root.name.lower() == subdir_name.lower():
        return root
    return root / subdir_name


def path_dedupe_key(path):
    value = str(path)
    return value.lower() if IS_WINDOWS else value


def normalize_windows_key(value):
    return str(value).strip().strip('"').strip("'").upper()


def resolve_tools_ini_path(root, raw_path):
    value = str(raw_path).strip().strip('"').strip("'")
    if not value:
        return None
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = root / path
    return path


def parse_tools_ini(keil_path):
    root = Path(normalize_install_path(keil_path, "keil") or keil_path)
    tools_ini = root / "TOOLS.INI"
    if not tools_ini.exists():
        return "", "", ""

    content = ""
    for enc in ("utf-8", "gbk", "mbcs", "latin1"):
        try:
            content = tools_ini.read_text(encoding=enc, errors="ignore")
            if "[UV2]" in content or "[ARM]" in content:
                break
        except Exception:
            continue

    armcc_include = ""
    armclang_include = ""
    rte_path = ""
    current_section = ""

    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith((";", "#")):
            continue

        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1].strip().upper()
            continue

        if "=" in line:
            key, val = line.split("=", 1)
            key = key.strip().upper()
            
            val = val.split("#")[0].strip().strip('"').strip("'")

            # 抓取 RTEPATH
            if key == "RTEPATH" and not rte_path:
                rte = resolve_tools_ini_path(root, val)
                if rte and rte.is_dir():
                    rte_path = str(rte.resolve())

            if key == "PATH" and val:
                base = resolve_tools_ini_path(root, val)
                if base:
                    if current_section == "ARMCC":
                        inc = base / "include"
                        if inc.is_dir(): armcc_include = str(inc.resolve())
                    elif current_section == "ARMCLANG":
                        inc = base / "include"
                        if inc.is_dir(): armclang_include = str(inc.resolve())
                    elif current_section == "ARM":
                        inc = base / "ARMCC" / "include"
                        if inc.is_dir() and not armcc_include: armcc_include = str(inc.resolve())
                        inc = base / "ARMCLANG" / "include"
                        if inc.is_dir() and not armclang_include: armclang_include = str(inc.resolve())

    return armcc_include, armclang_include, rte_path


def keil_pack_cmsis_base(install_path):
    for base in keil_cmsis_bases(install_path):
        return base
    return None


def keil_cmsis_bases(install_path):
    root = Path(normalize_install_path(install_path, "keil") or install_path)
    _, _, rte_path = parse_tools_ini(root)
    candidates = []
    if rte_path:
        candidates.extend(keil_cmsis_base_candidates(rte_path))
    arm_root = ide_subdir_or_root(root, "ARM")
    candidates.extend([
        arm_root / "Packs" / "ARM" / "CMSIS",
        arm_root / "PACK" / "ARM" / "CMSIS",
        arm_root / "CMSIS",
    ])

    bases = []
    seen = set()
    for candidate in candidates:
        if candidate.name.upper() != "CMSIS":
            continue
        key = path_dedupe_key(candidate)
        if key in seen:
            continue
        seen.add(key)
        if candidate.is_dir():
            bases.append(candidate.resolve())
    return bases


def keil_cmsis_base_candidates(rte_path):
    rte = Path(rte_path)
    leaf = rte.name.lower()
    if leaf == "cmsis":
        candidates = [rte]
    elif leaf in {"packs", "pack"}:
        candidates = [rte / "ARM" / "CMSIS"]
    elif leaf == "arm":
        candidates = [
            rte / "Packs" / "ARM" / "CMSIS",
            rte / "PACK" / "ARM" / "CMSIS",
            rte / "CMSIS",
        ]
    else:
        candidates = [
            rte / "ARM" / "Packs" / "ARM" / "CMSIS",
            rte / "ARM" / "PACK" / "ARM" / "CMSIS",
            rte / "Packs" / "ARM" / "CMSIS",
            rte / "PACK" / "ARM" / "CMSIS",
            rte / "ARM" / "CMSIS",
        ]

    result = []
    seen = set()
    for candidate in candidates:
        if candidate.name.upper() != "CMSIS":
            continue
        key = str(candidate).lower()
        if key in seen:
            continue
        seen.add(key)
        result.append(candidate)
    return result


def cmsis_base(install_path, tool):
    root = Path(normalize_install_path(install_path, tool) or install_path)
    if tool == "keil":
        bases = keil_cmsis_bases(root)
        if bases:
            return bases[0]
        return ide_subdir_or_root(root, "ARM") / "CMSIS"
    return ide_subdir_or_root(root, "arm") / "CMSIS"


def find_cmsis_versions(install_path, tool):
    if tool == "keil":
        bases = keil_cmsis_bases(install_path)
    else:
        base = cmsis_base(install_path, tool)
        bases = [base] if base.is_dir() else []

    versions = {}
    for base in bases:
        direct_candidates = [
            ("default", base / "Core" / "Include"),
            ("legacy", base / "Include"),
        ]
        for label, direct in direct_candidates:
            if direct.is_dir():
                key = label if len(bases) == 1 else f"{base.parent.name}/{base.name}/{label}"
                versions[key] = str(direct.resolve())

        for child in sorted(base.iterdir()):
            if not child.is_dir():
                continue
            candidates = [
                child / "CMSIS" / "Core" / "Include",
                child / "Core" / "Include",
            ]
            for candidate in candidates:
                if candidate.is_dir():
                    key = child.name
                    if key in versions and versions[key] != str(candidate.resolve()):
                        key = f"{base.parent.name}/{child.name}"
                    versions[key] = str(candidate.resolve())
                    break
    return versions


def find_iar_c_include(iar_path, arch="arm"):
    root = Path(normalize_install_path(iar_path, "iar") or iar_path)
    include = ide_subdir_or_root(root, arch) / "inc" / "c"
    return str(include.resolve()) if include.is_dir() else ""


def find_iar_inc(iar_path, arch="rl78"):
    root = Path(normalize_install_path(iar_path, "iar") or iar_path)
    include = ide_subdir_or_root(root, arch) / "inc"
    return str(include.resolve()) if include.is_dir() else ""


def find_uv4_executable(config_manager=None, override=None):
    candidates = []
    if override:
        candidates.append(Path(override).expanduser())

    if config_manager:
        keil_path = config_manager.get("keil", "install_path")
        if keil_path:
            root = Path(keil_path)
            candidates.extend([
                root / "UV4" / "UV4.exe",
                root.parent / "UV4" / "UV4.exe" if root.name.lower() == "arm" else root / "UV4" / "UV4.exe",
            ])

    candidates.extend([
        Path(r"C:\Keil_v5\UV4\UV4.exe"),
        Path(r"C:\Keil\UV4\UV4.exe"),
    ])

    path_value = os.environ.get("PATH", "")
    for path_dir in path_value.split(os.pathsep):
        if path_dir:
            candidates.append(Path(path_dir) / "UV4.exe")

    seen = set()
    for candidate in candidates:
        key = str(candidate).lower() if IS_WINDOWS else str(candidate)
        if key in seen:
            continue
        seen.add(key)
        if candidate.exists() and candidate.is_file():
            return str(candidate.resolve())
    return ""


def parse_keil_targets(project_file):
    tree = ET.parse(project_file)
    root = tree.getroot()
    targets = []
    for elem in root.findall(".//Target/TargetName"):
        if elem.text and elem.text.strip():
            targets.append(elem.text.strip())
    return targets


def normalize_keil_target_name(value):
    return str(value or "").strip().casefold()


def find_keil_target(root, target_name=None):
    targets = []
    for target in root.findall(".//Target"):
        name_elem = target.find("TargetName")
        if name_elem is None or not name_elem.text or not name_elem.text.strip():
            continue
        targets.append((name_elem.text.strip(), target))

    if not targets:
        return None, ""
    if target_name:
        requested = normalize_keil_target_name(target_name)
        for name, target in targets:
            if normalize_keil_target_name(name) == requested:
                return target, name
        available = ", ".join(name for name, _ in targets)
        raise ValueError(f"Keil target not found: {target_name}. Available targets: {available}")
    if len(targets) == 1:
        return targets[0][1], targets[0][0]

    index = choose_from_list("Select Keil target for compile_commands.json:", [name for name, _ in targets])
    if index <= 0:
        print(f"No target selected, defaulting to: {targets[0][0]}")
        return targets[0][1], targets[0][0]
    return targets[index - 1][1], targets[index - 1][0]


def build_keil_uv4_command(uv4, project_file, output, action, target=None, jobs=None, show_window=False):
    action_map = {
        "build": "-b",
        "rebuild": "-r",
        "clean": "-c",
        "flash": "-f",
        "download": "-f",
        "debug": "-d",
    }
    action = (action or "build").lower()
    if action not in action_map:
        raise ValueError(f"unsupported Keil action: {action}")

    command = [uv4]
    if action != "debug" and not show_window:
        if jobs is None:
            jobs = 16 if action == "build" else 0
        command.append(f"-j{jobs}")
    command.extend([
        action_map[action],
        str(project_file),
        "-o",
        str(output),
    ])
    if target:
        command.extend(["-t", target])
    return command


def print_new_log_content(log_file, position):
    if not log_file.exists():
        return position
    with log_file.open("rb") as f:
        f.seek(position)
        data = f.read()
        position = f.tell()
    if data:
        text = data.decode("utf-8", errors="replace")
        if "\ufffd" in text:
            text = data.decode("mbcs" if IS_WINDOWS else "utf-8", errors="replace")
        text = text.replace("\\", "/")
        encoding = sys.stdout.encoding or "utf-8"
        text = text.encode(encoding, errors="replace").decode(encoding, errors="replace")
        print(text, end="", flush=True)
    return position


def run_keil_uv4(project_path, action, target=None, jobs=None, show_window=False,
                uv4_path=None, log_path=None, config_manager=None, list_targets=False):
    if not IS_WINDOWS:
        raise RuntimeError("Keil UV4 command execution is only supported on Windows.")

    detector = CompileCommandsGenerator(
        path=project_path,
        config_manager=config_manager,
        target=target,
        project_type="keil",
    )
    project_file = detector.detect_project()
    if project_file.suffix.lower() != ".uvprojx":
        raise RuntimeError(f"Keil UV4 requires a .uvprojx project, got: {project_file}")

    targets = parse_keil_targets(project_file)
    if list_targets:
        print(f"Project: {project_file}")
        if targets:
            print("Targets:")
            for item in targets:
                print(f"  {item}")
        else:
            print("No TargetName found.")
        return 0

    if target and targets and not any(normalize_keil_target_name(target) == normalize_keil_target_name(item) for item in targets):
        print(f"Warning: target '{target}' was not found in project target list.")
        print("Available targets:")
        for item in targets:
            print(f"  {item}")

    uv4 = find_uv4_executable(config_manager=config_manager, override=uv4_path)
    if not uv4:
        raise FileNotFoundError("UV4.exe was not found. Configure Keil path with --setup or pass --keil_uv4.")

    action = (action or "build").lower()
    if action not in {"build", "rebuild", "clean", "flash", "download", "debug"}:
        raise ValueError(f"unsupported Keil action: {action}")

    output = Path(log_path).expanduser() if log_path else detector.project_root / ("build_log" if action == "build" else "Prg_Output")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.write_text("", encoding="utf-8")

    command = build_keil_uv4_command(uv4, project_file, output, action, target, jobs, show_window)

    print("Running:", " ".join(f'"{arg}"' if " " in arg else arg for arg in command))
    creationflags = 0
    if IS_WINDOWS and not show_window and action != "debug":
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

    process = subprocess.Popen(
        command,
        cwd=str(detector.project_root),
        creationflags=creationflags,
    )

    log_position = 0
    while process.poll() is None:
        try:
            log_position = print_new_log_content(output, log_position)
        except OSError:
            pass
        time.sleep(0.1)
    try:
        log_position = print_new_log_content(output, log_position)
    except OSError:
        pass

    print(f"Keil {action} finished with exit code {process.returncode}.")
    if action == "debug" and not show_window:
        print("Debug action always starts the Keil window.")
    return process.returncode


def detect_keil_compiler_type_from_root(root):
    uac6 = root.find(".//uAC6")
    if uac6 is not None and uac6.text:
        try:
            if int(uac6.text.strip()) > 0:
                return "armclang"
        except ValueError:
            pass

    pcc = root.find(".//pCCUsed")
    if pcc is not None and pcc.text:
        text = pcc.text.lower()
        if "armclang" in text or "ac6" in text:
            return "armclang"
        if "armcc" in text:
            return "armcc"

    return "armcc"


def detect_keil_compiler_type(project_file):
    tree = ET.parse(project_file)
    return detect_keil_compiler_type_from_root(tree.getroot())


def prompt_path(message):
    try:
        value = input(message).strip().strip('"')
    except EOFError:
        print("No interactive input available, skipped.")
        return ""
    if not value:
        return ""
    path = Path(value).expanduser()
    if path.exists():
        return str(path.resolve())
    print(f"  Path does not exist, skipped: {value}")
    return ""


def choose_from_list(title, values):
    print(f"\n{title}")
    for index, value in enumerate(values, 1):
        print(f"  [{index}] {value}")
    print("  [0] Skip / manual")
    while True:
        try:
            raw = input("Select: ").strip()
        except EOFError:
            print("No interactive input available, skipped.")
            return 0
        if raw == "":
            raw = "0"
        try:
            index = int(raw)
        except ValueError:
            print("Invalid selection.")
            continue
        if 0 <= index <= len(values):
            return index
        print("Invalid selection.")


def choose_cmsis(tool_name, install_path):
    versions = find_cmsis_versions(install_path, tool_name)
    if not versions:
        print(f"No CMSIS include path found under {cmsis_base(install_path, tool_name)}")
        return prompt_path("Enter CMSIS Core Include path manually (Enter to skip): ")

    keys = sorted(versions.keys())
    labels = [f"{key}: {versions[key]}" for key in keys]
    index = choose_from_list(f"Select {tool_name.upper()} CMSIS version:", labels)
    if index > 0:
        return versions[keys[index - 1]]
    return prompt_path("Enter CMSIS Core Include path manually (Enter to skip): ")


def first_run_setup(config_manager):
    config = config_manager.config
    print("Keil2Json first-run setup")
    print(f"Config will be stored at: {config_manager.path}")

    keil_paths = RegistryScanner.find_keil()
    if keil_paths:
        index = choose_from_list("Detected Keil installations:", keil_paths)
        keil_path = keil_paths[index - 1] if index > 0 else prompt_path("Enter Keil install path (Enter to skip): ")
    else:
        print("Keil was not detected from registry.")
        keil_path = prompt_path("Enter Keil install path (Enter to skip): ")

    if keil_path:
        config["keil"]["install_path"] = keil_path
        config["keil"]["cmsis_path"] = choose_cmsis("keil", keil_path)
        armcc_include, armclang_include, _ = parse_tools_ini(keil_path)
        config["keil"]["armcc_include"] = armcc_include
        config["keil"]["armclang_include"] = armclang_include
        if not armcc_include and not armclang_include:
            print("TOOLS.INI was not found or no ARMCC/ARMCLANG include path was detected.")
    else:
        print("Keil configuration skipped.")

    iar_arm_paths = RegistryScanner.find_iar_by_arch("arm")
    if iar_arm_paths:
        index = choose_from_list("Detected IAR for ARM installations:", iar_arm_paths)
        iar_arm_path = iar_arm_paths[index - 1] if index > 0 else prompt_path("Enter IAR for ARM install path (Enter to skip): ")
    else:
        iar_arm_path = prompt_path("Enter IAR for ARM install path (Enter to skip): ")

    if iar_arm_path:
        config["iar"]["arm_install_path"] = iar_arm_path
        config["iar"]["arm_cmsis_path"] = choose_cmsis("iar", iar_arm_path)
        config["iar"]["arm_c_include"] = find_iar_c_include(iar_arm_path, "arm")
        if not config["iar"]["arm_c_include"]:
            print("IAR C include path was not found at arm/inc/c.")
            config["iar"]["arm_c_include"] = prompt_path("Enter IAR ARM C include path (Enter to skip): ")
        config["iar"]["install_path"] = iar_arm_path
        config["iar"]["cmsis_path"] = config["iar"]["arm_cmsis_path"]
        config["iar"]["c_include"] = config["iar"]["arm_c_include"]
    else:
        print("IAR for ARM configuration skipped.")

    iar_rl78_paths = RegistryScanner.find_iar_by_arch("rl78")
    if iar_rl78_paths:
        index = choose_from_list("Detected IAR for RL78 installations:", iar_rl78_paths)
        iar_rl78_path = iar_rl78_paths[index - 1] if index > 0 else prompt_path("Enter IAR for RL78 install path (Enter to skip): ")
    else:
        iar_rl78_path = prompt_path("Enter IAR for RL78 install path (Enter to skip): ")

    if iar_rl78_path:
        config["iar"]["rl78_install_path"] = iar_rl78_path
        config["iar"]["rl78_c_include"] = find_iar_c_include(iar_rl78_path, "rl78")
        if not config["iar"]["rl78_c_include"]:
            print("IAR C include path was not found at rl78/inc/c.")
            config["iar"]["rl78_c_include"] = prompt_path("Enter IAR RL78 C include path (Enter to skip): ")
        config["iar"]["rl78_inc"] = find_iar_inc(iar_rl78_path, "rl78")
        if not config["iar"]["rl78_inc"]:
            print("IAR inc path was not found at rl78/inc.")
            config["iar"]["rl78_inc"] = prompt_path("Enter IAR RL78 inc path (Enter to skip): ")
    else:
        print("IAR for RL78 configuration skipped.")

    config_manager.save()
    print(f"Configuration saved: {config_manager.path}")


def parse_eww(eww_file):
    tree = ET.parse(eww_file)
    root = tree.getroot()
    ws_dir = eww_file.parent.resolve()
    existing = []
    missing = []
    for elem in root.findall(".//project/path"):
        if not elem.text or not elem.text.strip():
            continue
        raw_path = elem.text.strip().replace("$WS_DIR$", str(ws_dir))
        p = Path(raw_path)
        if not p.is_absolute():
            p = ws_dir / p
        p = p.resolve()
        if p.exists() and p.is_file():
            existing.append(p)
        else:
            missing.append(p)
    return existing, missing


def parse_ewp_configurations(file_path):
    tree = ET.parse(file_path)
    root = tree.getroot()
    configs = []
    for cfg in root.findall(".//configuration"):
        name_elem = cfg.find("name")
        if name_elem is not None and name_elem.text and name_elem.text.strip():
            configs.append(name_elem.text.strip())
    return configs


def parse_ewp_toolchain(file_path):
    tree = ET.parse(file_path)
    root = tree.getroot()
    tc = root.find(".//toolchain/name")
    if tc is not None and tc.text and tc.text.strip():
        return tc.text.strip().lower()
    return "arm"


class CompileCommandsGenerator:
    def __init__(self, path=None, absolute=False, config_manager=None, dry_run=False, target=None, project_type=None):
        self.path = Path(path).expanduser() if path and str(path).strip() else Path.cwd()
        self.absolute = absolute
        self.config_manager = config_manager or ConfigManager()
        self.dry_run = dry_run
        self.target = target
        self.project_type = project_type
        self.project_root = None
        self.compiler = "arm-none-eabi-gcc"
        self.extra_args = ["-D__GNUC__"]
        self.missing_iar = []
        self.selected_config = None

    def unique(self, items):
        seen = set()
        output = []
        for item in items:
            if not item:
                continue
            key = str(item).replace("\\", "/").lower()
            if key in seen:
                continue
            seen.add(key)
            output.append(str(item))
        return output

    def format_path(self, value):
        path = Path(value).resolve()
        if self.absolute:
            return str(path).replace("\\", "/")
        try:
            return os.path.relpath(str(path), str(self.project_root)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")

    def resolve_project_path(self, base, value):
        value = value.strip().strip('"').replace("\\", "/")
        if not value:
            return ""
        path = Path(value)
        if path.is_absolute():
            return str(path.resolve()).replace("\\", "/")
        return str((base / path).resolve()).replace("\\", "/")

    def parse_uvprojx(self, file_path):
        tree = ET.parse(file_path)
        root = tree.getroot()
        target_root, target_name = find_keil_target(root, self.target)
        parse_root = target_root if target_root is not None else root
        if target_name:
            print(f"Using Keil target: {target_name}")
        include_paths = []
        defines = []

        controls = parse_root.find(".//TargetArmAds/Cads/VariousControls")
        if controls is not None:
            include_elem = controls.find("IncludePath")
            if include_elem is not None and include_elem.text:
                include_paths.extend(include_elem.text.split(";"))
            define_elem = controls.find("Define")
            if define_elem is not None and define_elem.text:
                defines.extend(define_elem.text.split(","))

        abs_includes = []
        for include in include_paths:
            resolved = self.resolve_project_path(self.project_root, include)
            if resolved:
                abs_includes.append(resolved)

        compiler_type = detect_keil_compiler_type_from_root(parse_root)
        cmsis = self.config_manager.get("keil", "cmsis_path")
        if cmsis:
            abs_includes.append(cmsis)
        if compiler_type == "armclang":
            toolchain_include = self.config_manager.get("keil", "armclang_include")
        else:
            toolchain_include = self.config_manager.get("keil", "armcc_include")
        if toolchain_include:
            abs_includes.append(toolchain_include)

        source_files = []
        for group in parse_root.findall(".//Group"):
            for file_elem in group.findall(".//File"):
                path_elem = file_elem.find("FilePath")
                if path_elem is None or not path_elem.text:
                    continue
                source = self.resolve_project_path(self.project_root, path_elem.text)
                if source:
                    source_files.append(source)

        return self.unique(abs_includes), self.unique([d.strip() for d in defines]), self.unique(source_files)

    def parse_ewp(self, file_path, target_config=None):
        tree = ET.parse(file_path)
        root = tree.getroot()
        ewp_dir = Path(file_path).parent.resolve()

        configs = []
        cfg_nodes = {}
        for cfg in root.findall(".//configuration"):
            name_elem = cfg.find("name")
            if name_elem is not None and name_elem.text and name_elem.text.strip():
                cfg_name = name_elem.text.strip()
                configs.append(cfg_name)
                cfg_nodes[cfg_name] = cfg

        chosen_config_name = None
        if target_config:
            for c in configs:
                if c.casefold() == target_config.casefold():
                    chosen_config_name = c
                    break
        if not chosen_config_name and configs:
            if len(configs) == 1:
                chosen_config_name = configs[0]
            else:
                default_name = next((c for c in configs if c.casefold() == "debug"), configs[0])
                if sys.stdin.isatty():
                    idx = choose_from_list(f"Select IAR configuration for {file_path.name}:", configs)
                    chosen_config_name = configs[idx - 1] if idx > 0 else default_name
                else:
                    chosen_config_name = default_name

        if chosen_config_name:
            print(f"Using IAR configuration: {chosen_config_name}")
            parse_nodes = [cfg_nodes[chosen_config_name]]
        else:
            parse_nodes = root.findall(".//configuration")

        toolchain = "arm"
        for pnode in parse_nodes:
            tc_elem = pnode.find(".//toolchain/name")
            if tc_elem is not None and tc_elem.text:
                toolchain = tc_elem.text.strip().lower()
                break

        include_paths = []
        defines = []
        source_files = []

        for pnode in parse_nodes:
            for option in pnode.findall(".//settings/data/option"):
                name_elem = option.find("name")
                if name_elem is None or not name_elem.text:
                    continue
                values = [s.text.strip() for s in option.findall("state") if s.text and s.text.strip()]
                option_name = name_elem.text.strip()
                if option_name in {"CCIncludePath2", "CCIncludePath"}:
                    include_paths.extend(values)
                elif option_name in {"CCDefines", "CCDefines2"}:
                    defines.extend(values)

        for file_elem in root.findall(".//file"):
            name_elem = file_elem.find("name")
            if name_elem is None or not name_elem.text:
                continue
            value = name_elem.text.strip().replace("$PROJ_DIR$", str(ewp_dir))
            source = self.resolve_project_path(self.project_root, value)
            if source and self.is_source_file(source):
                source_files.append(source)

        abs_includes = []
        for include in include_paths:
            include = include.replace("$PROJ_DIR$", str(ewp_dir))
            resolved = self.resolve_project_path(self.project_root, include)
            if resolved:
                abs_includes.append(resolved)

        if toolchain == "rl78":
            self.compiler = "rl78-elf-gcc"
            self.extra_args = [
                "-D__GNUC__",
                "-D__ICCRL78__=1",
                "-D__near=",
                "-D__far=",
                "-D__saddr=",
                "-D__sfr=",
                "-D__callt=",
                "-D__interrupt=",
                "-D__root=",
                "-D__no_init=",
            ]
            c_inc = self.config_manager.get("iar", "rl78_c_include")
            inc = self.config_manager.get("iar", "rl78_inc")
            if not c_inc or not inc:
                rl78_root = self.config_manager.get("iar", "rl78_install_path")
                if rl78_root:
                    p_root = Path(rl78_root)
                    if not c_inc:
                        c_dir = ide_subdir_or_root(p_root, "rl78") / "inc" / "c"
                        if c_dir.is_dir():
                            c_inc = str(c_dir.resolve())
                    if not inc:
                        inc_dir = ide_subdir_or_root(p_root, "rl78") / "inc"
                        if inc_dir.is_dir():
                            inc = str(inc_dir.resolve())
            if c_inc:
                abs_includes.append(c_inc)
            if inc:
                abs_includes.append(inc)
        else:
            self.compiler = "arm-none-eabi-gcc"
            self.extra_args = ["-D__GNUC__"]
            cmsis = self.config_manager.get("iar", "arm_cmsis_path") or self.config_manager.get("iar", "cmsis_path")
            c_inc = self.config_manager.get("iar", "arm_c_include") or self.config_manager.get("iar", "c_include")
            if not cmsis or not c_inc:
                arm_root = self.config_manager.get("iar", "arm_install_path") or self.config_manager.get("iar", "install_path")
                if arm_root:
                    p_root = Path(arm_root)
                    if not cmsis:
                        cmsis = cmsis_base(p_root, "iar")
                    if not c_inc:
                        c_inc = find_iar_c_include(p_root, "arm")
            if cmsis:
                abs_includes.append(str(cmsis))
            if c_inc:
                abs_includes.append(str(c_inc))

        return self.unique(abs_includes), self.unique([d.strip() for d in defines]), self.unique(source_files)

    def shell_split(self, value):
        try:
            return shlex.split(value, posix=True)
        except ValueError:
            return []

    def is_source_file(self, value):
        return value.lower().endswith((".c", ".cc", ".cpp", ".cxx", ".s", ".S"))

    def is_compiler_command(self, tokens):
        if not tokens:
            return False
        compiler = Path(tokens[0].lstrip("@").strip('"')).name.lower()
        return compiler.endswith(("gcc", "g++", "cc", "clang", "clang++"))

    def parse_compile_command(self, line):
        tokens = self.shell_split(line)
        if not self.is_compiler_command(tokens) or "-c" not in tokens:
            return None

        compiler = tokens[0].lstrip("@")
        filtered_args = []
        source_file = ""
        skip_next = False
        for token in tokens[1:]:
            if skip_next:
                skip_next = False
                continue
            if token == "-o":
                skip_next = True
                continue
            if token.startswith("-o") and token != "-o":
                continue
            if token.startswith("-M"):
                if token in {"-MF", "-MT", "-MQ"}:
                    skip_next = True
                continue
            if self.is_source_file(token):
                source_file = token
                filtered_args.append(token)
                continue
            filtered_args.append(token)

        if not source_file:
            return None
        return compiler, source_file, filtered_args

    def run_make_command(self, args):
        return subprocess.run(
            args,
            cwd=self.project_root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

    def parse_makefile(self):
        makefile = self.project_root / "Makefile"
        if not makefile.exists():
            makefile = self.project_root / "makefile"
        if not makefile.exists():
            raise FileNotFoundError("cannot find Makefile or makefile")

        print("Running: make clean")
        clean_result = self.run_make_command(["make", "clean"])
        if clean_result.returncode != 0:
            print(f"Warning: make clean returned {clean_result.returncode}")

        print("Running: make -n")
        dry_result = self.run_make_command(["make", "-n"])
        if dry_result.returncode != 0:
            print(f"Warning: make -n returned {dry_result.returncode}")

        if not self.dry_run:
            print("Running: make")
            build_result = self.run_make_command(["make"])
            if build_result.returncode != 0:
                print(f"Warning: make returned {build_result.returncode}")

        lines = dry_result.stdout.splitlines() + dry_result.stderr.splitlines()
        entries = []
        for line in lines:
            parsed = self.parse_compile_command(line.strip())
            if not parsed:
                continue
            compiler, source, args = parsed
            source_path = Path(source)
            if not source_path.is_absolute():
                source_path = self.project_root / source_path
            entries.append((compiler, str(source_path.resolve()).replace("\\", "/"), args))

        if not entries:
            raise RuntimeError("no compile commands found from make -n output")
        return entries

    def generate_entries(self, include_paths, defines, source_files):
        compile_dir = str(self.project_root).replace("\\", "/")
        includes = [self.format_path(p) for p in self.unique(include_paths)]
        defines = self.unique(defines)
        base_args = self.extra_args + [f"-I{p}" for p in includes] + [f"-D{d}" for d in defines]
        entries = []
        for source in source_files:
            file_arg = self.format_path(source)
            args = [self.compiler, "-c", file_arg] + base_args
            entries.append({
                "command": " ".join(shlex.quote(a) for a in args),
                "arguments": args,
                "directory": compile_dir,
                "file": file_arg,
            })
        return entries

    def format_make_arg_path(self, value):
        path = Path(value)
        if not path.is_absolute():
            path = self.project_root / path
        return self.format_path(path)

    def generate_make_entries(self, compile_entries):
        compile_dir = str(self.project_root).replace("\\", "/")
        entries = []
        for compiler, source_file, args in compile_entries:
            formatted = []
            index = 0
            while index < len(args):
                token = args[index]
                if self.is_source_file(token):
                    formatted.append(self.format_path(source_file))
                    index += 1
                    continue
                if token == "-I" and index + 1 < len(args):
                    formatted.extend(["-I", self.format_make_arg_path(args[index + 1])])
                    index += 2
                    continue
                if token.startswith("-I") and len(token) > 2:
                    formatted.append("-I" + self.format_make_arg_path(token[2:]))
                    index += 1
                    continue
                formatted.append(token)
                index += 1
            command_args = [compiler] + formatted
            entries.append({
                "command": " ".join(shlex.quote(a) for a in command_args),
                "arguments": command_args,
                "directory": compile_dir,
                "file": self.format_path(source_file),
            })
        return entries

    def write_json(self, entries):
        output = self.project_root / "compile_commands.json"
        with output.open("w", encoding="utf-8") as f:
            json.dump(entries, f, indent=4, ensure_ascii=False)
        return output

    def collect_projects(self):
        root = self.path.resolve()
        projects = {"keil": [], "iar": [], "makefile": []}
        missing_iar = []
        if root.is_file():
            suffix = root.suffix.lower()
            name = root.name.lower()
            if suffix == ".uvprojx":
                return {"keil": [root], "iar": [], "makefile": []}, []
            if suffix == ".ewp":
                return {"keil": [], "iar": [root], "makefile": []}, []
            if suffix == ".eww":
                existing, missing = parse_eww(root)
                return {"keil": [], "iar": existing, "makefile": []}, missing
            if name == "makefile":
                return {"keil": [], "iar": [], "makefile": [root.parent.resolve()]}, []
            return projects, []

        projects["keil"] = sorted(root.glob("**/*.uvprojx"))
        if (root / "Makefile").exists() or (root / "makefile").exists():
            projects["makefile"].append(root)

        eww_files = sorted(root.glob("*.eww")) + sorted(root.glob("*/*.eww"))
        seen_ewp = set()
        iar_files = []
        for eww in eww_files:
            existing, missing = parse_eww(eww)
            missing_iar.extend(missing)
            for p in existing:
                key = str(p).lower() if IS_WINDOWS else str(p)
                if key not in seen_ewp:
                    seen_ewp.add(key)
                    iar_files.append(p)

        for p in sorted(root.glob("**/*.ewp")):
            key = str(p.resolve()).lower() if IS_WINDOWS else str(p.resolve())
            if key not in seen_ewp:
                seen_ewp.add(key)
                iar_files.append(p.resolve())

        projects["iar"] = iar_files
        self.missing_iar = missing_iar
        return projects, missing_iar

    def choose_project_file(self, project_type, files):
        if project_type == "makefile":
            return files[0]
        if project_type == "keil" and self.target:
            matched = []
            requested = normalize_keil_target_name(self.target)
            for path in files:
                try:
                    targets = parse_keil_targets(path)
                    if any(normalize_keil_target_name(item) == requested for item in targets):
                        matched.append(path)
                except ET.ParseError:
                    continue
            if len(matched) == 1:
                return matched[0]
            if len(matched) > 1:
                files = matched
        if len(files) == 1:
            return files[0]
        labels = [str(path) for path in files]
        index = choose_from_list(f"Select {project_type} project file:", labels)
        if index <= 0:
            print(f"No project file selected, defaulting to: {files[0]}")
            return files[0]
        return files[index - 1]

    def choose_iar_project_and_config(self, files):
        target_ewp = None
        target_cfg = None

        if self.target:
            raw_target = self.target.strip()
            if ":" in raw_target or "/" in raw_target:
                parts = re.split(r"[:/]", raw_target, maxsplit=1)
                target_ewp = parts[0].strip()
                target_cfg = parts[1].strip()
            else:
                for f in files:
                    if f.stem.casefold() == raw_target.casefold():
                        target_ewp = f.stem
                        break
                if not target_ewp:
                    for f in files:
                        cfgs = parse_ewp_configurations(f)
                        if any(c.casefold() == raw_target.casefold() for c in cfgs):
                            target_cfg = raw_target
                            target_ewp = f.stem
                            break

        if target_ewp:
            matched = [f for f in files if f.stem.casefold() == target_ewp.casefold()]
            if not matched:
                available = ", ".join(f.stem for f in files)
                raise ValueError(f"IAR project not found: {target_ewp}. Available projects: {available}")
            chosen_file = matched[0]
        elif len(files) == 1:
            chosen_file = files[0]
        else:
            labels = [str(path) for path in files]
            index = choose_from_list("Select IAR project file:", labels)
            if index <= 0:
                print(f"No project file selected, defaulting to: {files[0]}")
                chosen_file = files[0]
            else:
                chosen_file = files[index - 1]

        configs = parse_ewp_configurations(chosen_file)
        chosen_config = None
        if target_cfg:
            for c in configs:
                if c.casefold() == target_cfg.casefold():
                    chosen_config = c
                    break
            if not chosen_config:
                available = ", ".join(configs)
                raise ValueError(f"IAR configuration not found: {target_cfg}. Available configurations: {available}")
        elif len(configs) == 1:
            chosen_config = configs[0]
        elif configs:
            default_config = next((c for c in configs if c.casefold() == "debug"), configs[0])
            if sys.stdin.isatty():
                index = choose_from_list(f"Select IAR configuration for {chosen_file.name}:", configs)
                if index > 0:
                    chosen_config = configs[index - 1]
                else:
                    print(f"No configuration selected, defaulting to: {default_config}")
                    chosen_config = default_config
            else:
                chosen_config = default_config

        return chosen_file, chosen_config

    def detect_project(self):
        projects, missing_iar = self.collect_projects()
        available = [(kind, files) for kind, files in projects.items() if files]
        if not available:
            raise FileNotFoundError("cannot find .uvprojx, .ewp, .eww, Makefile, or makefile")

        if self.project_type:
            kind = self.project_type.lower()
            if kind == "make":
                kind = "makefile"
            if kind not in projects:
                raise ValueError(f"unsupported project type: {self.project_type}")
            if not projects[kind]:
                raise FileNotFoundError(f"cannot find project type: {self.project_type}")
        elif len(available) == 1:
            kind = available[0][0]
        else:
            labels = []
            for item_kind, files in available:
                labels.append(f"{item_kind}: {files[0]}" + (f" ({len(files)} files)" if len(files) > 1 else ""))
            index = choose_from_list("Multiple project types found, select generator branch:", labels)
            if index <= 0:
                print(f"No project type selected, defaulting to: {available[0][0]}")
                kind = available[0][0]
            else:
                kind = available[index - 1][0]

        if kind == "makefile":
            project = projects[kind][0]
            self.project_root = project.resolve()
            return project / "Makefile"

        if kind == "keil":
            project = self.choose_project_file(kind, projects[kind])
            self.project_root = self.path.resolve() if self.path.is_dir() else (project.parent.resolve() if project.is_file() else self.path.resolve())
            return project

        project, selected_config = self.choose_iar_project_and_config(projects["iar"])
        self.selected_config = selected_config
        self.project_root = self.path.resolve() if self.path.is_dir() else project.parent.resolve()
        return project

    def generate(self):
        project_file = self.detect_project()
        suffix = project_file.suffix.lower()
        name = project_file.name.lower()

        if suffix == ".uvprojx":
            print("Detected Keil project")
            includes, defines, sources = self.parse_uvprojx(project_file)
            entries = self.generate_entries(includes, defines, sources)
        elif suffix == ".ewp":
            toolchain = parse_ewp_toolchain(project_file).upper()
            print(f"Detected IAR {toolchain} project")
            includes, defines, sources = self.parse_ewp(project_file, getattr(self, "selected_config", None))
            entries = self.generate_entries(includes, defines, sources)
        elif name in {"makefile"}:
            print("Detected Makefile project")
            entries = self.generate_make_entries(self.parse_makefile())
        else:
            raise ValueError(f"unsupported project file: {project_file}")

        output = self.write_json(entries)
        style = "absolute" if self.absolute else "relative"
        print(f"generate complete: {output} ({style} path, {len(entries)} files)")


def list_project_targets(project_path, config_manager=None, project_type=None):
    generator = CompileCommandsGenerator(
        path=project_path,
        config_manager=config_manager,
        project_type=project_type,
    )
    projects, missing_iar = generator.collect_projects()

    if projects["keil"]:
        print("=== Keil MDK Projects ===")
        for prj in projects["keil"]:
            print(f"Project: {prj}")
            targets = parse_keil_targets(prj)
            if targets:
                print("  Targets:")
                for item in targets:
                    print(f"    {item}")
            else:
                print("  No TargetName found.")

    if projects["iar"]:
        print("=== IAR Projects ===")
        for prj in projects["iar"]:
            toolchain = parse_ewp_toolchain(prj).upper()
            configs = parse_ewp_configurations(prj)
            print(f"Project: {prj.stem} ({prj})")
            print(f"  Toolchain: {toolchain}")
            if configs:
                print("  Configurations:")
                for c in configs:
                    print(f"    {c}")
            else:
                print("  No configurations found.")

    if missing_iar:
        for m in missing_iar:
            print(f"Warning: Referenced project does not exist: {m}")

    if not projects["keil"] and not projects["iar"]:
        if projects["makefile"]:
            print("=== Makefile Project ===")
            print(f"Path: {projects['makefile'][0]}")
        else:
            print("No Keil or IAR projects found.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json for Keil MDK, IAR EWARM, and Makefile projects"
    )
    parser.add_argument("positional_path", nargs="?", default=None, help="Project path or project file path")
    parser.add_argument("--path", "-p", required=False, help="Project path or project file path")
    parser.add_argument("--absolute", "-a", action="store_true", help="Format paths as absolute")
    parser.add_argument("--project-type", choices=["keil", "iar", "makefile", "make"], help="Select generator branch when multiple project types exist")
    parser.add_argument("--setup", "-s", action="store_true", help="Run setup wizard and save config")
    parser.add_argument("--show-config", action="store_true", help="Print saved config and exit")
    parser.add_argument("--dry-run", "-n", action="store_true", help="For Makefile projects use make -n after make clean")
    parser.add_argument("--keil_build", action="store_true", help="Run Keil UV4 command instead of generating compile_commands.json")
    parser.add_argument(
        "--keil_action",
        choices=["build", "rebuild", "clean", "flash", "download", "debug"],
        default="build",
        help="Keil UV4 action used with --keil_build",
    )
    parser.add_argument("--target", "-t", help="Target / configuration name used for compile_commands.json generation or --keil_build")
    parser.add_argument("--list-targets", action="store_true", help="List targets and exit")
    parser.add_argument("--keil_uv4", help="Override UV4.exe path")
    parser.add_argument("--keil_jobs", type=int, help="Keil UV4 -j value used when hiding the Keil window; debug never uses -j")
    parser.add_argument("--keil_log", help="Keil UV4 output log path")
    parser.add_argument("--keil_window", action="store_true", help="Show Keil window while running UV4; debug always shows the window")
    args = parser.parse_args()

    project_path = args.path or args.positional_path

    manager = ConfigManager()
    if args.show_config:
        print(f"Config file: {manager.path}")
        print(json.dumps(manager.config, indent=4, ensure_ascii=False))
        return

    if args.setup or not manager.exists():
        first_run_setup(manager)
        if args.setup and not project_path and not args.keil_build and not args.list_targets:
            return

    try:
        if args.list_targets:
            return list_project_targets(
                project_path=project_path,
                config_manager=manager,
                project_type=args.project_type,
            )

        if args.keil_build:
            return run_keil_uv4(
                project_path=project_path,
                action=args.keil_action,
                target=args.target,
                jobs=args.keil_jobs,
                show_window=args.keil_window or args.keil_action == "debug",
                uv4_path=args.keil_uv4,
                log_path=args.keil_log,
                config_manager=manager,
                list_targets=False,
            )

        generator = CompileCommandsGenerator(
            path=project_path,
            absolute=args.absolute,
            config_manager=manager,
            dry_run=args.dry_run,
            target=args.target,
            project_type=args.project_type,
        )
        generator.generate()
        return 0
    except (FileNotFoundError, ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())


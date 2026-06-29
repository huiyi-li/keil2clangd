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
        "version": 1,
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


def normalize_install_path(value, tool):
    if not value:
        return None
    path = Path(str(value).strip().strip('"')).expanduser()
    if not path.exists() or not path.is_dir():
        return None

    path = path.resolve()
    leaf = path.name.lower()

    # Registry values may point to C:\Keil_v5\ARM or C:\IAR...\arm.
    # Keep install_path as the IDE root so later path derivation does not add ARM/arm twice.
    if tool == "keil" and leaf == "arm":
        return str(path.parent)
    if tool == "iar" and leaf == "arm":
        return str(path.parent)

    # Some registry values point one level below the root, for example a product/toolchain folder.
    if tool == "keil":
        if (path / "ARM" / "CMSIS").is_dir() or (path / "TOOLS.INI").exists():
            return str(path)
        if (path.parent / "ARM" / "CMSIS").is_dir() or (path.parent / "TOOLS.INI").exists():
            return str(path.parent.resolve())
    elif tool == "iar":
        if (path / "arm" / "CMSIS").is_dir() or (path / "arm" / "inc" / "c").is_dir():
            return str(path)
        if (path.parent / "arm" / "CMSIS").is_dir() or (path.parent / "arm" / "inc" / "c").is_dir():
            return str(path.parent.resolve())

    return str(path)


def ide_subdir_or_root(install_path, subdir_name):
    root = Path(install_path)
    if root.name.lower() == subdir_name.lower():
        return root
    return root / subdir_name


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

    parser = configparser.ConfigParser()
    try:
        parser.read(tools_ini, encoding="utf-8")
    except configparser.Error:
        parser.read(tools_ini, encoding="mbcs")

    armcc_include = ""
    armclang_include = ""
    rte_path = ""
    for section in parser.sections():
        if parser.has_option(section, "RTEPATH") and not rte_path:
            rte = resolve_tools_ini_path(root, parser.get(section, "RTEPATH"))
            if rte and rte.is_dir():
                rte_path = str(rte.resolve())

        if not parser.has_option(section, "PATH"):
            continue
        base = resolve_tools_ini_path(root, parser.get(section, "PATH"))
        if not base:
            continue
        name = section.upper()

        if name == "ARMCC":
            include = base / "include"
            if include.is_dir():
                armcc_include = str(include.resolve())
        elif name == "ARMCLANG":
            include = base / "include"
            if include.is_dir():
                armclang_include = str(include.resolve())
        elif name == "ARM":
            include = base / "ARMCC" / "include"
            if include.is_dir() and not armcc_include:
                armcc_include = str(include.resolve())
            include = base / "ARMCLANG" / "include"
            if include.is_dir() and not armclang_include:
                armclang_include = str(include.resolve())

    return armcc_include, armclang_include, rte_path


def keil_pack_cmsis_base(install_path):
    _, _, rte_path = parse_tools_ini(install_path)
    if rte_path:
        for candidate in keil_cmsis_base_candidates(rte_path):
            if candidate.is_dir():
                return candidate.resolve()
    return None


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
        pack_base = keil_pack_cmsis_base(root)
        if pack_base:
            return pack_base
        return ide_subdir_or_root(root, "ARM") / "CMSIS"
    return ide_subdir_or_root(root, "arm") / "CMSIS"


def find_cmsis_versions(install_path, tool):
    base = cmsis_base(install_path, tool)
    if not base.is_dir():
        return {}

    versions = {}
    direct = base / "Core" / "Include"
    if direct.is_dir():
        versions["default"] = str(direct.resolve())

    for child in sorted(base.iterdir()):
        if not child.is_dir():
            continue
        candidates = [
            child / "CMSIS" / "Core" / "Include",
            child / "Core" / "Include",
        ]
        for candidate in candidates:
            if candidate.is_dir():
                versions[child.name] = str(candidate.resolve())
                break
    return versions


def find_iar_c_include(iar_path):
    root = Path(normalize_install_path(iar_path, "iar") or iar_path)
    include = ide_subdir_or_root(root, "arm") / "inc" / "c"
    return str(include.resolve()) if include.is_dir() else ""


def detect_keil_compiler_type(project_file):
    tree = ET.parse(project_file)
    root = tree.getroot()

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

    iar_paths = RegistryScanner.find_iar()
    if iar_paths:
        index = choose_from_list("Detected IAR installations:", iar_paths)
        iar_path = iar_paths[index - 1] if index > 0 else prompt_path("Enter IAR install path (Enter to skip): ")
    else:
        print("IAR was not detected from registry.")
        iar_path = prompt_path("Enter IAR install path (Enter to skip): ")

    if iar_path:
        config["iar"]["install_path"] = iar_path
        config["iar"]["cmsis_path"] = choose_cmsis("iar", iar_path)
        config["iar"]["c_include"] = find_iar_c_include(iar_path)
        if not config["iar"]["c_include"]:
            print("IAR C include path was not found at arm/inc/c.")

    config_manager.save()
    print(f"Configuration saved: {config_manager.path}")


class CompileCommandsGenerator:
    def __init__(self, path=None, absolute=False, config_manager=None, dry_run=False):
        self.path = Path(path).expanduser() if path and str(path).strip() else Path.cwd()
        self.absolute = absolute
        self.config_manager = config_manager or ConfigManager()
        self.dry_run = dry_run
        self.project_root = None
        self.compiler = "arm-none-eabi-gcc"
        self.extra_args = ["-D__GNUC__"]

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
        include_paths = []
        defines = []

        controls = root.find(".//TargetArmAds/Cads/VariousControls")
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

        compiler_type = detect_keil_compiler_type(file_path)
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
        for group in root.findall(".//Group"):
            for file_elem in group.findall(".//File"):
                path_elem = file_elem.find("FilePath")
                if path_elem is None or not path_elem.text:
                    continue
                source = self.resolve_project_path(self.project_root, path_elem.text)
                if source:
                    source_files.append(source)

        return self.unique(abs_includes), self.unique([d.strip() for d in defines]), self.unique(source_files)

    def parse_ewp(self, file_path):
        tree = ET.parse(file_path)
        root = tree.getroot()
        include_paths = []
        defines = []
        source_files = []

        for option in root.findall(".//configuration/settings/data/option"):
            name_elem = option.find("name")
            if name_elem is None or not name_elem.text:
                continue
            values = [s.text.strip() for s in option.findall("state") if s.text and s.text.strip()]
            option_name = name_elem.text.strip()
            if option_name in {"CCIncludePath2", "CCIncludePath"}:
                include_paths.extend(values)
            elif option_name in {"CCDefines", "CCDefines2"}:
                defines.extend(values)

        for group in root.findall(".//group"):
            for file_elem in group.findall("file"):
                name_elem = file_elem.find("name")
                if name_elem is None or not name_elem.text:
                    continue
                value = name_elem.text.strip().replace("$PROJ_DIR$", ".")
                source = self.resolve_project_path(self.project_root, value)
                if source:
                    source_files.append(source)

        abs_includes = []
        for include in include_paths:
            include = include.replace("$PROJ_DIR$", ".")
            resolved = self.resolve_project_path(self.project_root, include)
            if resolved:
                abs_includes.append(resolved)

        for key in ("cmsis_path", "c_include"):
            include = self.config_manager.get("iar", key)
            if include:
                abs_includes.append(include)

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

    def detect_project(self):
        root = self.path.resolve()
        if root.is_file():
            self.project_root = root.parent.resolve()
            return root

        self.project_root = root
        for pattern in ("**/*.uvprojx", "**/*.ewp"):
            files = list(root.glob(pattern))
            if files:
                self.project_root = files[0].parent.resolve()
                return files[0]
        for name in ("Makefile", "makefile"):
            makefile = root / name
            if makefile.exists():
                return makefile
        raise FileNotFoundError("cannot find .uvprojx, .ewp, Makefile, or makefile")

    def generate(self):
        project_file = self.detect_project()
        suffix = project_file.suffix.lower()
        name = project_file.name.lower()

        if suffix == ".uvprojx":
            compiler_type = detect_keil_compiler_type(project_file)
            print(f"Detected Keil project, compiler: {compiler_type}")
            includes, defines, sources = self.parse_uvprojx(project_file)
            entries = self.generate_entries(includes, defines, sources)
        elif suffix == ".ewp":
            print("Detected IAR EWARM project")
            includes, defines, sources = self.parse_ewp(project_file)
            entries = self.generate_entries(includes, defines, sources)
        elif name in {"makefile"}:
            print("Detected Makefile project")
            entries = self.generate_make_entries(self.parse_makefile())
        else:
            raise ValueError(f"unsupported project file: {project_file}")

        output = self.write_json(entries)
        style = "absolute" if self.absolute else "relative"
        print(f"generate complete: {output} ({style} path, {len(entries)} files)")


def main():
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json for Keil MDK, IAR EWARM, and Makefile projects"
    )
    parser.add_argument("--path", "-p", required=False, help="Project path or project file path")
    parser.add_argument("--absolute", "-a", action="store_true", help="Format paths as absolute")
    parser.add_argument("--setup", "-s", action="store_true", help="Run setup wizard and save config")
    parser.add_argument("--show-config", action="store_true", help="Print saved config and exit")
    parser.add_argument("--dry-run", "-n", action="store_true", help="For Makefile projects use make -n after make clean")
    args = parser.parse_args()

    manager = ConfigManager()
    if args.show_config:
        print(f"Config file: {manager.path}")
        print(json.dumps(manager.config, indent=4, ensure_ascii=False))
        return

    if args.setup or not manager.exists():
        first_run_setup(manager)
        if args.setup and not args.path:
            return

    generator = CompileCommandsGenerator(
        path=args.path,
        absolute=args.absolute,
        config_manager=manager,
        dry_run=args.dry_run,
    )
    generator.generate()


if __name__ == "__main__":
    main()

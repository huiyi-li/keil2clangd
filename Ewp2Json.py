import os
import json
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET
import shlex

class EwpCompileCommandsGenerator:
    def __init__(self, path=None, absolute=False):
        self.path = path if path else '.'
        self.absolute = absolute
        self.project_root = None
        self.include_paths = []
        self.defines = []
        self.source_files = []

    def parse_ewp(self, file_path, project_root):
        # 解析XML文件
        tree = ET.parse(file_path)
        root = tree.getroot()
        
        # 解析defines
        defines = []
        include_paths = []
        # 首先找到ICCARM settings
        iccarm_settings = root.find('.//settings[name="ICCARM"]')
        if iccarm_settings is not None:
            # 在ICCARM settings中找到CCDefines选项
            cc_defines = iccarm_settings.find('.//option[name="CCDefines"]')
            if cc_defines is not None:
                # 获取所有state节点的内容
                for state in cc_defines.findall('state'):
                    if state.text:
                        defines.append(state.text.strip())
                        # print(f"Define: {state.text.strip()}")
            # 解析include paths
            cc_include_paths = iccarm_settings.find('.//option[name="CCIncludePath2"]')
            if cc_include_paths is not None:
                # 获取所有state节点的内容
                for path in cc_include_paths.findall('state'):
                    if path.text:
                        print(f"Include Path: {path.text.strip()}")
                        # 处理 $PROJ_DIR$ 宏
                        clean_path = path.text.strip().replace('$PROJ_DIR$', '.')
                        clean_path = clean_path.replace('\\', '/')
                        if not clean_path:
                            continue
                        # 构建绝对路径
                        abs_path = (project_root / clean_path).resolve()
                        include_paths.append(str(abs_path).replace('\\', '/'))
        
        # 解析源文件
        source_files = []
        for group in root.findall('.//group'):
            group_name = group.find('name')
            if group_name is not None:
                for file_elem in group.findall('.//file'):
                    file_path = file_elem.find('name')
                    if file_path is not None and file_path.text:
                        # 处理 $PROJ_DIR$ 宏
                        clean_path = file_path.text.strip().replace('$PROJ_DIR$', '.')
                        clean_path = clean_path.replace('\\', '/')
                        # 构建绝对路径
                        abs_file_path = (project_root / clean_path).resolve()
                        source_files.append(str(abs_file_path).replace('\\', '/'))
        
        return include_paths, defines, source_files

    def generate_entries(self, include_paths, defines, source_files):
        # 获取 compile_commands.json 所在目录的绝对路径
        compile_dir = self.project_root
        compile_dir_str = str(compile_dir).replace("\\", "/")
        
        # 处理 Include 路径
        processed_include_paths = []
        for path in include_paths:
            abs_path = Path(path).resolve()
            if not self.absolute:
                try:
                    rel_path = os.path.relpath(str(abs_path), str(compile_dir))
                    processed_include_paths.append(rel_path.replace("\\", "/"))
                except ValueError:
                    processed_include_paths.append(str(abs_path).replace("\\", "/"))
            else:
                processed_include_paths.append(str(abs_path).replace("\\", "/"))
        
        # 构建基础编译参数
        base_args = [
            "-D__GNUC__",
        ] + [f"-I{p}" for p in processed_include_paths] + \
            [f"-D{define}" for define in defines]
        
        compiler = "arm-none-eabi-gcc"
        
        # 处理源文件路径
        entries = []
        for file in source_files:
            file_path = Path(file).resolve()
            if not self.absolute:
                try:
                    rel_file = os.path.relpath(str(file_path), str(compile_dir))
                    file_entry = rel_file.replace("\\", "/")
                except ValueError:
                    file_entry = str(file_path).replace("\\", "/")
            else:
                file_entry = str(file_path).replace("\\", "/")

            command_str = compiler + " " + "-c " + file_entry + " " + " ".join(shlex.quote(arg) for arg in base_args)

            # 构建 JSON 条目
            entry = {
                "command": command_str,
                "arguments": base_args.copy(),
                "directory": compile_dir_str,
                "file": file_entry
            }
            entries.append(entry)
        
        return entries

    def write_json(self, entries):
        with open('compile_commands.json', 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=4, ensure_ascii=False)

    def generate(self):
        # 查找当前目录下的.ewp文件
        ewp_files = list(Path(self.path).glob('**/*.ewp'))
        if not ewp_files:
            print("cannot find any .ewp file in current directory")
            return
        
        # 处理第一个找到的.ewp文件
        ewp_path = ewp_files[0]
        self.project_root = ewp_path.parent.resolve()
        self.include_paths, self.defines, self.source_files = self.parse_ewp(ewp_path, self.project_root)
        entries = self.generate_entries(self.include_paths, self.defines, self.source_files)
        self.write_json(entries)
        print(f"generate complete: compile_commands.json ({'absolute path' if self.absolute else 'relative path'})")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Generate compile_commands.json for IAR EWARM projects')
    parser.add_argument('--path', '-p', required=False, help='Specify the path of .ewp file')
    parser.add_argument('--absolute', '-a', action='store_true', required=False, help='Format with Absolute path')
    args = parser.parse_args()

    generator = EwpCompileCommandsGenerator(path=args.path, absolute=args.absolute)
    generator.generate()

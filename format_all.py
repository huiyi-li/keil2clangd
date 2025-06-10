import os
import json
import argparse
import xml.etree.ElementTree as ET
from pathlib import Path

def parse_uvprojx(file_path, project_root):
    # 解析XML文件
    tree = ET.parse(file_path)
    root = tree.getroot()

    # 精确查找 IncludePath 和 Define
    various_controls = root.find('.//TargetArmAds/Cads/VariousControls')
    include_paths = []
    defines = []

    if various_controls is not None:
        # 提取 IncludePath
        include_elem = various_controls.find('IncludePath')
        if include_elem is not None and include_elem.text:
            include_paths = include_elem.text.split(';')

        # 提取 Define
        define_elem = various_controls.find('Define')
        if define_elem is not None and define_elem.text:
            defines = define_elem.text.split(',')

    # 转换IncludePath为绝对路径
    absolute_include_paths = []
    for path in include_paths:
        clean_path = path.strip().replace('\\', '/')
        if not clean_path:
            continue
        # 构建绝对路径
        abs_path = (project_root / clean_path).resolve()
        absolute_include_paths.append(str(abs_path).replace('\\', '/'))

    # 处理Define中的空格
    defines = [d.strip() for d in defines if d.strip()]

    # 获取所有源文件路径并转换绝对路径
    source_files = []
    for group in root.findall('.//Group'):
        for file_elem in group.findall('.//File'):
            file_path_elem = file_elem.find('FilePath')
            if file_path_elem is not None and file_path_elem.text:
                file_path = file_path_elem.text.strip().replace('\\', '/')
                # 构建绝对路径
                abs_file_path = (project_root / file_path).resolve()
                source_files.append(str(abs_file_path).replace('\\', '/'))

    return absolute_include_paths, defines, source_files

def generate_absolute_compile_commands(path):

    if path is None:
        path = '.'
    # 查找当前目录下的uvprojx文件
    uvprojx_files = list(Path(path).glob('**/*.uvprojx'))
    if not uvprojx_files:
        print("未找到.uvprojx文件")
        return

    # 处理第一个找到的uvprojx文件
    uvprojx_path = uvprojx_files[0]
    project_root = uvprojx_path.parent.resolve()  # 项目根目录
    include_paths, defines, source_files = parse_uvprojx(uvprojx_path, project_root)

    # 构建基础参数（使用绝对路径）
    base_args = [
        "-c",
        "-D__GNUC__",
    ] + [f"-I{path}" for path in include_paths] + \
      [f"-D{define}" for define in defines]

    # 获取目录信息
    current_dir = str(project_root).replace('\\', '/')  # 项目根目录绝对路径

    # 生成条目
    entries = []
    for file in source_files:
        entry = {
            "arguments": base_args.copy(),
            "directory": current_dir,
            "file": file  # 直接使用绝对路径
        }
        entries.append(entry)

    # 写入JSON文件
    # with open('compile_commands.json', 'w', encoding='utf-8') as f:
    with open('compile_commands.json', 'w') as f:
        json.dump(entries, f, indent=4, ensure_ascii=False)

    print("生成完成：compile_commands.json（绝对路径版）")

def generate_compile_commands(path):

    if path is None:
        path = '.'
    # 查找当前目录下的uvprojx文件
    uvprojx_files = list(Path(path).glob('**/*.uvprojx'))
    if not uvprojx_files:
        print("未找到.uvprojx文件")
        return

    # 处理第一个找到的uvprojx文件
    uvprojx_path = uvprojx_files[0]
    include_paths, defines, source_files = parse_uvprojx(uvprojx_path)

    # 构建基础参数
    base_args = [
        "-c",
        "-D__GNUC__",
    ] + [f"-I{path}" for path in include_paths] + \
      [f"-D{define}" for define in defines]

    # 获取当前目录路径（转换为Windows风格）
    current_dir = str(Path.cwd().resolve()).replace('\\', '/')

    # 生成条目
    entries = []
    for file in source_files:
        entry = {
            "arguments": base_args.copy(),
            "directory": current_dir,
            "file": f"./{file}" if not file.startswith('./') else file
        }
        entries.append(entry)

    # 写入JSON文件
    # with open('compile_commands.json', 'w', encoding='utf-8') as f:
    with open('compile_commands.json', 'w') as f:
        json.dump(entries, f, indent=4, ensure_ascii=False)

    print("生成完成：compile_commands.json")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Generate compile_commands.json for vscode')
    parser.add_argument('--path', '-p', required=False, help='Specify the path of .uvprojx file')
    parser.add_argument('--absolute ', '-a', required=False, help='format Absolute path ')
    args = parser.parse_args()
    if args.absolute:
            generate_absolute_compile_commands(args.path)
    else:
        generate_compile_commands(args.path)
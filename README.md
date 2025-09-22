# 为Keil MDK以及IAR开发环境生成 clangd和vscode的c插件生成对应的配置文件脚本

本工具添加IAR的支持，使用方式与keil相同，为了方便不同用户使用，二者暂不合并，IAR用户使用的脚本名称为Ewp2Json.py，Keil用户使用的脚本名称为xx2Json.py
使用方式完全一致，请根据需要选择使用即可。

## 使用方式

- 克隆本仓库到本地

``` bash
https://github.com/huiyi-li/keil2clangd.git
```
**注意： xxx代表Keil或Ewp**
- 复制xxx2Json.py脚本到工程项目目录下。
- 使用python执行脚本(默认搜索当前终端路径的keil prj文件生成对应的配置文件)

``` bash
python xxx2Json.py
```

- 脚本会在目录生成对应的compile_commands.json文件，使用该文件可以帮助插件进行跳转解析。
- 注意这里的默认生成方式是递归查找当前文件夹内部的keil prj文件。如果有多个，默认处理第一个keil文件。
- 可以输入`-h`查看帮助信息。

## 扩展选项

- `-p` 指定搜索路径，默认是当前路径。
示例：

``` bash
python xx2Json.py -p D:\KeilProject
```

- `-a` 指定生成的compalte_commands.json文件的.c和include path为绝对路径。
示例：

``` bash
python xx2Json.py -a
```

- -p 和 -a 选项可以同时使用。

## 使用release页面使用pyinstaller打包成exe文件的脚本

- 下载release页面的xx2Json.exe文件。
- 将xx2Json.exe文件防止到系统的path环境变量目录中。
例如：
我的电脑设置了`C:\MinGW\bin`为系统环境变量，将Kiil2Json.exe文件复制到`C:\MinGW\bin`目录下。
- 打开cmd，输入`xx2Json.exe`命令即可直接运行，不需要每次将exe复制到keil工程目录下。
- 注意：keil工程目录下必须有keil uvprojx文件，否则会提示找不到文件。
- 使用脚本生成的时候建议使用powershell，cd到工程所在目录

## 注意事项

- 脚本默认使用python3.x版本。
- 对于uint32_t等类型无法正常识别，需要在工程文件夹下创建.clangd文件，并添加以下内容：
- 新增includ路径也可以在clangd文件中加入对应选项，例如 `-ICMSIS/Core/Include`
``` yaml
CompileFlags:
  Add: [-include, stdint.h, -ICMSIS/Core/Include]
```

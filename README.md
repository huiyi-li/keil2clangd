# 为Keil MDK开发环境生成 clangd和vscode的c插件生成对应的配置文件脚本

## 使用方式

- 克隆本仓库到本地
- 使用python执行脚本(默认搜索当前终端路径的keil prj文件生成对应的配置文件)

``` bash
python format_all.py
```

- 脚本会在目录生成对应的compile_commands.json文件，使用该文件可以帮助插件进行跳转解析。

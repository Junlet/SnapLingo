# 构建与打包

本文档说明如何把本项目编译成可执行程序，并打包成 Windows 安装包。

## 一、环境准备

本仓库需要 **Qt 6（MSVC x64）**、**CMake**、**MSVC 编译器** 和 **Inno Setup**。
下面这套是已经在本机验证通过的组合。

| 组件 | 版本 | 位置 |
|---|---|---|
| Visual Studio 生成工具 | VS 2026 (18.10)，MSVC 14.51 | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools` |
| Windows SDK | 10.0.26100.0 | 随 VS 安装 |
| CMake | 4.4.5 | `%APPDATA%\Python\Python314\Scripts\cmake.exe` |
| Ninja | 1.13.2 | 同上 |
| Qt | 6.8.3 msvc2022_64 | `C:\Qt\6.8.3\msvc2022_64` |
| Inno Setup | 6.7.3（仅当前用户） | `%LOCALAPPDATA%\Programs\Inno Setup 6` |

> Qt 只需要 `qtbase` 和 `qtsvg` 两个归档即可满足本项目的
> `Widgets / Network / Gui / Core / Concurrent / Sql` 依赖。

### 在新机器上重建这套环境

```powershell
# 1) CMake / Ninja / Qt 下载器（按用户安装，无需管理员）
pip install --user cmake ninja aqtinstall

# 2) Qt 6.8.3（约 270 MB）
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --archives qtbase qtsvg -O C:\Qt

# 3) 安装包制作工具（仅当前用户）
winget install --id JRSoftware.InnoSetup -e
```

MSVC 编译器通过 Visual Studio Installer 勾选
「使用 C++ 的桌面开发」工作负载获得（只需生成工具即可，不需要完整 IDE）。

## 二、一条命令打包

在仓库根目录（本文件所在目录）执行：

```powershell
.\build.ps1
```

脚本会依次完成：

1. 用 `Visual Studio` 生成器配置 CMake（x64，Release）
2. 编译到 `build\Release\`
3. 用 `windeployqt` 收集 Qt 插件与 MSVC 运行库
4. 用 Inno Setup 编译 `setup.iss`

### 常用参数

```powershell
# 指定其它 Qt 版本
.\build.ps1 -QtDir C:\Qt\6.10.3\msvc2022_64

# 只编译，不生成安装包
.\build.ps1 -SkipInstaller
```

> `-SkipDeploy` 可以跳过 windeployqt，但那样产出的 exe 会因为找不到 Qt DLL
> 而无法启动，一般只在排查问题时使用。

## 三、产物位置

| 产物 | 路径 | 说明 |
|---|---|---|
| 程序（已带依赖） | `build\Release\` | 可直接双击运行，约 82 MB |
| 安装包 | `setup\SnapLingo_Setup.exe` | 约 38 MB |

新安装会装到 `%ProgramFiles%\SnapLingo`，创建开始菜单与桌面快捷方式，并支持
覆盖升级与静默卸载。

**旧版本升级不搬目录。** 安装包的 AppId 保持不变，Inno Setup 默认沿用上一次的
安装目录，所以从 `AI Screenshot Translator` 升级时程序仍留在原文件夹（例如
`D:\Apps\AI Screenshot Translator`（示例）），`storage\` 与 `.env` 原地保留。安装完成后
只清理旧名字的快捷方式和旧 exe，开始菜单会新建 SnapLingo 分组，卸载列表、
开始菜单和桌面快捷方式都会显示 SnapLingo。

## 启动参数

| 参数 | 行为 |
|---|---|
| 无参数 | 打开主页（普通启动默认） |
| `--screenshot` | 直接开始截图（应用图标 / 桌面快捷方式） |
| `--dashboard` | 打开主页（首次启动、开机自启、托盘单击） |
| `--archive` | 打开归档窗口 |
| `--settings` | 打开设置 |
| `--tray` | 只驻留托盘 |

已有一个实例在运行时，第二次启动会把参数转发给现有实例并退出，不再弹
“已在运行”的提示。普通启动（无参数）打开主页；桌面/开始菜单快捷方式使用
`--screenshot`；首次启动和开机自启使用 `--dashboard`。不创建“主页”快捷方式，
主页也可以从托盘菜单进入。旧版本残留的 `--home` 快捷方式会按普通启动处理，
也就是打开主页。

托盘图标单击默认 **打开主页**，托盘菜单第一项也是“打开主页”。如果更希望
单击直接截图，可在 设置 → 系统 → 高级设置 → **托盘单击** 中改成“开始截图”。

主页只通过标题栏的 **×** 关闭；点击空白处、滚动滚轮或按 Esc 都不会关闭。

安装目录里还会保留一份 `AI-Screenshot-Translator-Cpp.exe`，内容就是新的
`SnapLingo.exe`。这是给旧版固定到任务栏/快捷工具栏的图标用的兼容别名；旧图标
仍能启动，并且无参数启动会直接截图。卸载时会一并删除。

## 四、注意事项

**必须使用 Visual Studio 生成器。**
`setup.iss` 里写死了 `build\Release` 这个路径，而这个结构是多配置生成器
（Visual Studio）产出的；换成 Ninja 会得到 `build\` 单层目录，安装包就找不到文件。

**`.env` 是可选的。**
`setup.iss` 会尝试把根目录的 `.env` 一并打包（若存在），因此那一条 `Source`
带上了 `skipifsourcedoesntexist` 标记；仓库本身不提供 `.env`，缺失属于正常情况。

**windeployqt 会打印一条 translations 警告。**
内容大致是 `Cannot open .../translations/catalogs.json`。本项目界面文案走
`assets/translations/*.json`，不依赖 Qt 自带翻译，这条警告可以忽略。
若觉得干扰，可用 `aqt install-qt ... --archives qtbase qtsvg qttranslations` 补装。

**更换 Qt 版本后要清理构建缓存。**
`build\` 里缓存了 Qt 的绝对路径，换版本请先删掉 `build\` 再重新构建。

**默认不打包 OpenGL 软件渲染（`opengl32sw.dll`）。**
本项目界面基于 WebView2，不需要它，这样安装包小很多。

## 五、手动构建（不使用脚本）

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64\lib\cmake"
cmake --build build --config Release --parallel

windeployqt --release --no-translations --no-opengl-sw --compiler-runtime `
            "build\Release\SnapLingo.exe"

& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" setup.iss
```

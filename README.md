# 灵动岛 (Dynamic Island)

一个常驻桌面顶部的 Windows 时间胶囊，支持日期/秒钟/主题/位置/尺寸/自动隐藏等自定义选项。

## 功能特性

- 实时时间显示（每秒刷新）
- 胶囊形窗口（CreateRoundRectRgn）
- 鼠标悬停放大、离开缩回
- 左键打开系统时间设置，右键打开自定义设置对话框
- 深色、浅色、跟随系统主题
- 24/12 小时制、日期/年份/秒钟显示开关
- 窗口大小与位置可配置
- 自动隐藏 + 滑动动画
- 设置持久化到注册表

## 项目结构

```text
time-dynamic-island/
├── include/
│   ├── actions.h
│   ├── animation.h
│   ├── app_state.h
│   ├── layout.h
│   ├── rendering.h
│   ├── settings.h
│   └── time_theme.h
├── resources/
│   ├── DynamicIsland.rc
│   └── resource.h
├── src/
│   ├── actions.cpp
│   ├── animation.cpp
│   ├── app_state.cpp
│   ├── layout.cpp
│   ├── main.cpp
│   ├── rendering.cpp
│   ├── settings.cpp
│   └── time_theme.cpp
├── builds/                 # 构建中间产物目录
├── build.ps1               # MSVC 构建脚本
├── Makefile                # g++/windres 构建文件
└── README.md
```

## 构建方式

### 1) 使用 build.ps1 (MSVC, 推荐)

要求：Visual Studio 2019/2022（含 C++ 桌面开发组件）。

```powershell
.\build.ps1
```

输出说明：

- 可执行文件：DynamicIsland.exe（项目根目录）
- 中间产物：
  - builds/obj/*.obj
  - builds/res/DynamicIsland.res

### 2) 使用 Makefile (g++ / MinGW-w64)

要求：已安装 g++、windres、make（Windows 下通常是 mingw32-make）。

```bash
mingw32-make
```

或在支持 GNU make 的环境中：

```bash
make
```

输出说明：

- 可执行文件：DynamicIsland.exe（项目根目录）
- 中间产物：
  - builds/gpp/obj/*.o
  - builds/gpp/res/DynamicIsland_res.o

清理：

```bash
mingw32-make clean
```

## 运行

```powershell
.\DynamicIsland.exe
```

## 使用说明

- 鼠标悬停：窗口放大
- 左键点击：打开系统“日期和时间”设置
- 右键点击：打开灵动岛设置对话框

可在设置中调整：

- 时间格式（24/12 小时）
- 日期、年份、秒钟显示
- 主题（深色/浅色/系统）
- 窗口大小（Small/Medium/Large）
- 显示位置（Left/Center/Right）
- 自动隐藏延迟（分钟 + 秒）
- 字号与粗体

## 配置存储

程序配置保存到：

```text
HKEY_CURRENT_USER\SOFTWARE\DynamicIsland
```

自启动项保存到：

```text
HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run
```

## 常见问题

### 编译失败

- 确认已安装对应工具链（MSVC 或 MinGW-w64）
- 确认已安装 Windows SDK 与资源编译工具（rc.exe 或 windres）
- 优先先跑 build.ps1 验证环境

### 程序可运行但看不到窗口

- 检查屏幕顶部区域（Left/Center/Right）
- 确认未被自动隐藏（移动鼠标到顶部触发区域）
- 检查任务管理器中是否存在 DynamicIsland 进程

## 免责声明

本项目仅供学习与研究使用，使用风险由使用者自行承担。

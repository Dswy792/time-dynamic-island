# 灵动岛 (Dynamic Island)

一个 Windows 顶部时间胶囊：平时显示时间/日期，微信、QQ 来新消息时在胶囊右侧弹出圆形副岛。

纯 C++17 / Win32 / GDI+ 实现 · 单个 exe · 无 .NET / Java / 第三方运行库 · 零注入、不挂钩、不读进程内存

**版本 5.2.3** · Windows 10 / 11

## 功能

- **时间显示**：24/12 小时制、秒、上/下午；日期、年份、星期与多种日期格式可自由组合
- **外观**：深色 / 浅色 / 跟随系统；小 / 中 / 大三档尺寸；字体大小、粗体、不透明度可调
- **消息提醒**：微信（界面读取）与 QQ（系统通知）新消息弹出圆形副岛，内部为真实应用图标；悬停查看内容，点击打开应用并收起；默认常驻；支持隐私模式（只显示"有新消息"）
- **行为**：自动隐藏、全屏应用时隐藏、开机自启；托盘菜单；设置窗口内可直接打开 Windows 日期和时间设置
- **适配**：Per-Monitor V2 高 DPI、多显示器、跟随系统主题；左键打开设置、拖动移动位置
- **兼容**：不注入、不挂钩、不读写聊天数据库，对游戏反作弊友好

## 系统要求

- Windows 10 / 11 x64
- 编译：Visual Studio 2022（C++ 桌面开发工作负载）+ Windows SDK 10.0.17763 或更高（需其中 C++/WinRT 头文件）
- 运行：无需额外运行库

## 构建与运行

```powershell
git clone https://github.com/Dswy792/time-dynamic-island.git
cd time-dynamic-island

.\build.ps1              # 增量构建
.\build.ps1 -Clean       # 全量重建
.\DynamicIsland.exe      # 启动
```

- 构建中间文件写入 `builds\`，脚本通过 vswhere 自动定位 Visual Studio
- 只改一个模块时可快速语法检查：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\compile-check.ps1 -Files src\main.cpp -Tag main`
- 卸载 / 清理：`.\clean.ps1`（停止进程、删除 exe 与构建产物、清理开机自启和注册表设置）

## 使用

- **左键**灵动岛打开设置；**右键**无操作；按住拖动可移动位置，松手保存
- **托盘图标右键**：设置、日期和时间设置、开机自启动、重置位置、关于、退出
- 设置窗口：`Enter` 确定、`Esc` 取消；"退出程序"带二次确认
- **副岛**：悬停显示"发送者：内容"（隐私模式只显示"微信 有新消息"）；点击打开/恢复微信或 QQ 并收起该副岛；默认常驻，可在设置里改为 1–10 秒自动收起

## 消息提醒规则

**微信**（Windows UI Automation 读取会话列表，不注入、不读内存）

- 只提醒普通好友和未设"消息免打扰"的群聊
- 跳过公众号 / 订阅号 / 服务号、微信支付、服务通知、折叠的群聊等系统会话
- 文件传输助手始终提醒（从电脑发往手机也会提醒）
- 当前打开的会话仅在微信位于前台时跳过；微信最小化或在后台时照常提醒

**QQ**（读取 Windows 通知）

- 需在 QQ 设置中开启新消息通知，并在 Windows 设置中允许 QQ 通知，否则收不到提醒
- 按通知文本过滤：订阅号 / 公众号 / 服务号 / 看点 / QQ 空间 / 腾讯新闻等不提醒
- 免打扰 QQ 群聊是否提醒，取决于 QQ 自身是否推送

开关位于设置窗口"通知"卡片：消息提醒、显示内容、微信、QQ、停留时长。

## 配置

- 位置：`HKCU\SOFTWARE\DynamicIsland`（`SettingsVersion = 2`）
- 常用值：`Theme`、`WindowSize`、`Position`、`FontSize`、`BoldFont`、`Opacity`、`Use24Hour`、`ShowSeconds`、`ShowDate`、`DateFormat`、`AutoHide`、`HideDelaySeconds`、`HideOnFullscreen`、`NotifyEnable`、`NotifyShowContent`、`NotifyWeChat`、`NotifyQQ`、`NotifyPeekSeconds`（0 = 常驻）
- 手动修改注册表后重启程序生效；删除整个键恢复默认
- 开机自启由 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 中的 `DynamicIsland` 值控制

## 项目结构

```
src/                  实现（main / rendering / layout / animation / settings_dialog / notifications / chat_watcher / theme / time_format / tray / actions）
include/              头文件
resources/            图标、manifest、资源与版本信息
tools/                辅助脚本（compile-check.ps1、capture-screen.ps1）
build.ps1             一键构建
clean.ps1             卸载 / 清理
docs/架构说明.md       设计与实现细节
docs/灵动岛实现计划.md  开发计划
```

## 常见问题

- **看不到灵动岛**：检查是否被全屏应用隐藏，或使用托盘菜单"重置位置"
- **收不到微信提醒**：确认微信正在运行；微信大版本更新可能改变其界面结构
- **收不到 QQ 提醒**：确认 QQ 与 Windows 的通知开关均已打开
- **公众号 / 订阅号 / 免打扰群不提醒**：这是 v5.2.2 起的预期行为
- **重复启动**：默认单实例，再次运行会直接打开设置窗口

## 许可证与免责声明

- 本项目仅供个人学习和研究使用
- 按"现状"提供，不附带任何明示或暗示的保证；使用本程序造成的后果由使用者自行承担
- 请遵守当地法律与相关服务条款

---

版本 5.2.3 · 2026-10-30 · C++17 / Win32 / GDI+ / MSVC / C++/WinRT · UI Automation

# Linux 与 Windows 运行说明

本包同时提供 Windows `.bat` 和 Linux `.sh` 脚本。Windows 下优先使用 `.bat`，Linux 下优先使用 `.sh`。

## 主工程

Windows：

```bat
00_BUILD_ALL_WINDOWS.bat
01_RUN_ALL_TESTS_WINDOWS.bat
02_RUN_OS_WINDOWS.bat
```

Linux：

```bash
bash 00_BUILD_ALL_LINUX.sh
bash 01_RUN_ALL_TESTS_LINUX.sh
bash 02_RUN_OS_LINUX.sh
```

## 成员独立模块

Windows：

```bat
pre_integration\RUN_INTERACTIVE_DEMO_MENU_WINDOWS.bat
```

Linux：

```bash
cd pre_integration
bash RUN_INTERACTIVE_DEMO_MENU_LINUX.sh
```

如果 Linux 环境没有 `clang++`，脚本会自动尝试使用 `g++`。

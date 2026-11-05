[**English**](README.md) | [**中文版**](#)

# Smoke — 命令行笔记应用

用 C 编写的轻量命令行笔记应用。Smoke 支持快速添加、列出与编辑笔记，笔记自带时间戳。笔记长度、笔记条数、单条笔记的行数均无上限。

## 功能

- **添加笔记** — 新笔记自动带时间戳
- **编辑器模式** — `smoke add` 不带内容时打开默认编辑器。**整段文本保存为一条笔记**，包含换行与分段
- **列出笔记** — 按行号显示全部笔记。长笔记与多行笔记完整输出，不截断
- **编辑笔记** — 按行号修改。多行笔记在编辑器中打开
- **无长度限制** — 笔记、笔记文件、单行长度按需增长
- **写入安全** — 编辑先写临时文件再改名替换，崩溃或磁盘满不会损坏笔记
- **UTF-8 安全** — 多字节字符不会被劈成两半
- **跨平台** — 支持 Linux 与 Windows
- **持久存储** — 笔记保存在本地文本文件

## 安装

### Fedora / RHEL / CentOS (COPR)

```bash
sudo dnf copr enable xieguaiwu/smoke
sudo dnf install smoke
```

### 从源码编译

```bash
gcc smoke.c -o smoke
```

### 安装到系统（可选）

```bash
sudo cp smoke /usr/local/bin/
```

## 用法

```
smoke <command> [options]
```

### 命令

| 命令 | 说明 |
|------|------|
| `add [content]` | 添加新笔记。不带内容时打开默认编辑器 |
| `list` | 显示全部笔记 |
| `edit <line_number> [new_content]` | 按行号编辑笔记 |

### 示例

**添加新笔记：**
```bash
smoke add Buy groceries tomorrow
```

**用编辑器添加笔记（不带内容参数）：**
```bash
smoke add
```
此命令依次打开 `$VISUAL`、`$EDITOR`、`vi`（Windows 为 `notepad`）。

- 临时文件以 `#` 开头的行是提示行，保存时忽略。
- **整段文本保存为一条笔记**，无论写多少行、分多少段。
- 首尾空行丢弃，中间空行保留。
- 内容为空或编辑器非零退出时不保存任何内容。

**列出全部笔记：**
```bash
smoke list
```

输出：
```
=== Notes ===
1. [2026-03-10 14:30:00] Buy groceries tomorrow
2. [2026-03-11 09:12:44] 长笔记第一行
第二行
第三行
```

**编辑笔记：**
```bash
smoke edit 1 Buy groceries and cook dinner
```

或交互式编辑（不提供新内容）：
```bash
smoke edit 1
```
单行笔记在提示符中编辑；多行笔记在编辑器中打开，并预填当前内容。

## 存储位置

笔记以带时间戳的纯文本存储：

- **Linux**：`~/.local/share/notes.txt`
- **Windows**：`notes.txt`（当前目录）

目录不存在时，应用会自动创建，包括缺失的上级目录。

## 笔记格式

一条笔记就是一条记录。记录以方括号中的时间戳开头，同一条笔记的后续每一行以单个前导空格存储：

```
[2026-03-10 14:30:00] Buy groceries tomorrow
[2026-03-11 09:12:44] 长笔记第一行
 第二行
 第三行
```

该格式向后兼容：旧版本写入的「一行一条」文件可被正确读取。

## 检查已有笔记文件

旧版本有两个缺陷：编辑器里写的笔记被拆成一行一条，超过 1023 字节的笔记被拆成多条。`check_notes.sh` 报告这两类痕迹，只读不改。

```bash
./check_notes.sh ~/.local/share/notes.txt
```

## 退出码

| 码 | 含义 |
|:---:|:---|
| `0` | 成功，或无需保存（空笔记） |
| `1` | 写入失败、编辑器失败、行号非法、行号越界 |
| `2` | 用法错误（缺参数、未知命令） |

## 编译要求

- C 编译器（gcc、clang 等）
- 标准 C 库

## 测试

```bash
gcc -O2 -Wall -Wextra -std=c99 smoke.c -o smoke_test
./test_add_editor.sh
```

在地址与未定义行为消毒器下运行同一套测试：

```bash
clang -g -O1 -fsanitize=address,undefined -std=c99 smoke.c -o /tmp/smoke_asan
SMOKE_BIN=/tmp/smoke_asan ./test_add_editor.sh
```

## 许可证

本项目开源，仅供个人与学习使用。

## 贡献

欢迎提交 issue 与 pull request 报告缺陷或改进意见。

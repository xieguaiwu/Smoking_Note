[**English**](README.md) | [**中文版**](#)

# Smoke — 命令行笔记应用

用 C 编写的轻量命令行笔记应用。Smoke 支持快速添加、列出与编辑笔记，笔记自带时间戳。

## 功能

- **添加笔记** — 新笔记自动带时间戳
- **编辑器模式** — `smoke add` 不带内容时打开默认编辑器
- **列出笔记** — 按行号显示全部已保存笔记
- **编辑笔记** — 按行号修改已有笔记
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
| `list` | 显示全部已保存笔记 |
| `edit <line_number> [new_content]` | 按行号编辑已有笔记 |

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
临时文件以 `#` 开头的行为提示行。每个非空行保存为一条笔记。
内容为空或编辑器非零退出时不保存任何内容。

**列出全部笔记：**
```bash
smoke list
```

输出：
```
=== Notes ===
1. [2026-03-10 14:30:00] Buy groceries tomorrow
```

**编辑笔记：**
```bash
smoke edit 1 Buy groceries and cook dinner
```

或交互式编辑（不提供新内容）：
```bash
smoke edit 1
```

## 存储位置

笔记以带时间戳的纯文本存储：

- **Linux**：`~/.local/share/notes.txt`
- **Windows**：`notes.txt`（当前目录）

Linux 下如目录不存在，应用会自动创建。

## 笔记格式

每条笔记格式如下：
```
[YYYY-MM-DD HH:MM:SS] 笔记内容
```

## 编译要求

- C 编译器（gcc、clang 等）
- 标准 C 库

## 许可证

本项目开源，仅供个人与学习使用。

## 贡献

欢迎提交 issue 与 pull request 报告缺陷或改进功能。

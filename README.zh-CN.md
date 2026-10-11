# NX System On Linux（NXSOL）

**NX System** 是操作系统。**NXSOL** 是它当前形态的内部代号：底层是 Linux 内核，其上全部是我们自己的。

**NX 就是 NX**，不是任何缩写，不展开成任何东西。

- 底层：Linux 内核，走内核自带的 EFI stub 启动 —— **没有 GRUB**
- 许可证：GPL-2.0-only
- init：`nxinit`，为本项目所写
- 配置：**NX Vault**，由单一守护进程持有的带签名存储
- 包布局：`apt` 管 `/usr`，自研 `nxpkg` 管 `/nx`

---

## 版本

`26-1.0.0` —— 版本规范与历史见 `VERSION.md`。

简单说：年份，然后主/次/修订，其中主版本意味着"你会不得不注意到的变化"。1.0.0 是因为界面现在**真的**从 NX Vault 读外观了。它以前声称会读，实际没有：两个加载函数都是空壳，所以每个值都停在编译期默认，把 `caret-bounce` 设成 0.5 毫无反应。

---

## 为什么底层换成 Linux

项目最初是从零写的内核。它真的启动起来了 —— UEFI 入口、页表、GDT/IDT、长模式。这部分工作**归档而非删除**，在 `../NXS-archive/NX-Kernel-archived/`。

换到 Linux 只有一个原因：一个跑不了任何软件的桌面是演示品，不是系统。而用户真正摸得到的东西 —— 窗口按钮、配置编辑器、启动动画、打字回弹 —— 无论底层是谁，都是我们的。底层内核恰恰是"自己写"换不来任何用户可感知价值的那一部分。

为旧内核写的东西一件没扔。`window-controls.c` 和 `caret-bounce.c` 原样编译通过，现在还能编成 `libnxui.so`。

---

## 启动链路

```
UEFI 固件
  -> \EFI\BOOT\BOOTX64.EFI        NX Boot Manager（我们的）
       从 ESP 读 \vmlinuz 与 \initrd.img
       通过 Linux initrd media device path 递出 initramfs
       LoadImage + StartImage
  -> Linux 内核（EFI stub）
       执行 initramfs 里的 /init
  -> nxinit（PID 1）
       挂载 /proc /sys /dev
       启动 nxvaultd
       从 NX Vault 读 /system/boot/target
       读到就继续，读不到就停机说明
```

镜像里没有 GRUB。内核本身是 PE/COFF EFI 应用（`CONFIG_EFI_STUB`），固件可以直接执行它。

---

## NX Vault 是启动依赖

不是"用得上"，是**必须**。`nxinit` 在决定要启动什么之前，先从 vault 读 `/system/boot/target`，**读不到就带着说明停机**。一个绕开配置硬启动的系统，比一个明确说自己起不来的系统更糟。

### Vault 是什么

一组 `.nxv` 文件，由 `nxvaultd` 独占持有，别的进程一概不直接读写。其余所有程序 —— CLI 的 `nxvault`、GUI、桌面 —— 都是客户端，在抽象 Unix socket 上收发一行文本：

```
GET /system/boot/target        ->  OK normal
SET /system/boot/target rescue ->  ERR 1   （权限不足）
```

socket 之所以用抽象的（名字在内核里而非磁盘上），正是为了让 initramfs 在挂载任何文件系统之前就能用上它。

### 分层

| 层级 | 谁可写 | 例子 |
|------|--------|------|
| user | 属主账号 | `/user/interface/typing/caret-bounce` |
| admin | root | `/system/display/theme` |
| **immutable** | 仅维护模式 | `/system/boot/*`、`/system/kernel/*`、`/system/security/*` |

root 也不足以改 immutable 键。启动时就会验证：`nxinit` 试着写 `/system/boot/target`，写入被拒 —— 这件事是被断言的，不是被假定的。

### 关于保证强度，说实话

在旧内核上，`.nxv` 的 inode 被打了标记，`open()` 对除 `nxvaultd` 以外的所有人失败，root 也算。Linux 没有这种标记，所以保证更弱，明说：

- 文件 0600，由守护进程持有打开
- 每个文件带校验和与备份
- 写入前按层级拒绝
- 校验不通过的文件不加载，改用上一版本

root 拿着十六进制编辑器仍能把文件改坏。那时系统会拒绝信任它并回退 —— 这才是真正要紧的性质。

### 它不是注册表

不叫注册表编辑器，也不是。没有可手改的文本文件，没有可加载的 hive，除了走守护进程之外没有任何途径能碰到那些值。

---

## 启动模式

| 模式 | 怎么进 | 跑什么 |
|------|--------|--------|
| normal | 默认 | 完整系统 |
| rescue | `nxinit` 的 target | 最小；不启动 nxvaultd，配置只读 |
| maintenance | NX+Alt 然后从电源菜单重启，或用 U 盘镜像 | 全部可写，含 immutable 键 |

退出 `fs-borderless-locked` 全屏用 `Ctrl+Alt+Esc`。

---

## NXIcons

14 个字形：三个窗口按钮、五个窗口模式、三个终端状态、音量、静音、设置。码位 U+E100–E10D，私有区。全程无 emoji —— 每个图标都是几何图形。

轮廓**编译进 `libnxicons.so`**，不从字体加载。字体文件可能缺失、损坏，或者比第一帧需要的时间更慢，任何一种都会让窗口上"关闭"按钮的位置留一个空白。框架不能有这种状态。`nxicons.ttf` 照旧生成，给文档和想要真正字体的应用用，但合成器里没有任何东西读它。

因此这个库以不可变方式安装：root 删不掉也换不了，维护模式是唯一能碰它的地方。在维护模式下删掉会怎样，见 [docs/nxicons-immutable.zh-CN.md](docs/nxicons-immutable.zh-CN.md)。

任何地方都不用 emoji 当功能图标。每个图标都是 `nxicons.ttf` 里的几何字形，位于 U+E100 起的私有区，按当前文字颜色绘制，由字体引擎像其它字符一样缩放。

分成两个共享库，只需要窗口控件的程序不必带上系统图标：

| 库 | 内容 |
|----|------|
| `libnxicons-core.so` | 红绿灯（3）、窗口模式（5） |
| `libnxicons-system.so` | 终端状态（3）、音量（2）、设置（1） |

`icons/gen_svg.py` 画字形，`icons/gen_font.py` 生成字体。

---

## NX 键

NX 键**就是** Windows 键 —— Ctrl 和 Alt 之间那个物理按键。NX System 叫它 NX 键。它不是先按 N 再按 X。组合键列表，以及怎么进恢复模式（`NX + Alt`，然后电源图标，然后重启），见 [docs/nx-key.zh-CN.md](docs/nx-key.zh-CN.md)。

---

## 窗口红绿灯

按钮在**右上角**，顺序 最小化 / 最大化 / 关闭。这是 Windows 的排布，故意的：迁移成本为零，比"为了不同而不同"更有价值。

*显示逻辑*是 macOS 与极简的混合 —— 颜色一直在，只是淡了一档，所以你能看出有按钮、也能分清哪个是哪个，但它从不与窗口内容争注意力：

| 状态 | 颜色 | 大小 | 图标 |
|------|------|------|------|
| 活动、鼠标不在标题栏 | 淡彩 | 8px | 无 |
| 鼠标进标题栏 | 全彩 | 10px | 无 |
| 悬浮到按钮 | 全彩 | 14px | 显示 |
| 未聚焦 | 灰色，0.40 不透明度 | 8px | 无 |
| 特权窗口 | 红/橙 | 10px | 不褪色 |

取值都在 vault 的 `/user/interface/window/controls/` 下：

| 键 | 含义 |
|----|------|
| `style` | `hybrid`（默认）/ `macos` / `minimal` |
| `color-mode` | `fixed`（默认）或 `saturation` |
| `idle-saturation` | 待机圆点向灰色拉低多少 |
| `hover-saturation` | 指针进入标题栏后的饱和度 |
| `inactive-opacity` | 未聚焦窗口的褪色程度 |
| `dot-idle-px` / `dot-hover-px` / `dot-focus-px` | 三档尺寸 |
| `anim-enter-ms` / `anim-leave-ms` | 动画时长 |

`color-mode` 决定 `idle-saturation` 起不起作用：

- **`fixed`**（默认）用手挑的那组淡色 —— `E8DCA0`、`A0D8A0`、`E8A0A0`。设计意图被原样保留，饱和度键不动它。
- **`saturation`** 按 `idle-saturation` 从全彩实时算，滑块才真的咬合：`1.0` 得到 `FFBD2F` / `28C840` / `FF5F57`，`0.0` 得到灰。

默认是 `fixed`，所以外观与这个键存在之前完全一致；想要滑块生效的人自己切。

---

## 窗口模式

全屏、边框、可拖动是三个轴，收敛成一个枚举，因为不成意义的组合不值得表达：

| 模式 | 全屏 | 边框 | 可拖动 | 按钮 |
|------|------|------|--------|------|
| `normal` | 否 | 是 | 是 | 显示 |
| `borderless` | 否 | 否 | 是 | 碰顶部才出 |
| `fullscreen` | 是 | 是 | 否 | 碰顶部才出 |
| `fs-borderless` | 是 | 否 | 否 | 碰顶部才出 |
| `fs-borderless-locked` | 是 | 否 | 否 | 永不绘制 |

---

## 构建与运行

```sh
./build.sh
```

它编译 `nxvaultd` 与 `nxinit`、打包 initramfs、编译引导器、生成磁盘镜像并在 QEMU 里跑起来。缺的工具会尝试自动安装。

预期输出：

```
Hello NXSOL!
Linux 5.15.0-198-generic
nxinit: starting NX Vault ... nxvaultd: listening on @nxvault
ok
NX Vault: target = normal
nxinit: boot target is immutable (even for root)
nxinit: milestone 2 reached, staying resident
```

Windows 10 跑不了 `.sh`。`tools/windows/` 下有 `fetch.ps1` 与 `fetch.cmd`，把官方安装包下到 `tools/windows/environment/` 就停手 —— 它们不安装任何东西，也不动 PATH。三个安装包你自己按顺序装，然后 `fetch.ps1 -CheckPath` 告诉你哪些成了。

---

## 目录

```
boot/       NX Boot Manager（UEFI 应用）
init/       nxinit，PID 1
vault/      nxvaultd、libnxvault、协议
icons/      NXIcons：SVG 源、字体生成器、共享库
userland/   libnxui（窗口控件、光标回弹）
oobe/       首次启动动画
tools/      mkinitramfs.py、mkdisk.py、windows/
```

## 构建

三个 workflow，在 `.github/workflows/`：

| workflow | 触发 | 产物 |
|----------|------|------|
| `minimal` | 每次 push | `nxsol-minimal.img` —— 引导+内核+initramfs，在 QEMU 里跑完并断言 |
| `full`    | 按钮或 tag | 完整系统 squashfs，切成带哈希的卷 |
| `iso`     | 按钮或 tag | `nxsol.iso`（离线）和 `nxsol-netinstall.iso`（网络） |

minimal 那个会在串口日志里断言 `Hello NXSOL`、`nxvaultd`、`boot target is
immutable`，缺一个就判失败。"编译过了"不等于"能启动"。

安装器与首次启动的设计在 [docs/installer.zh-CN.md](docs/installer.zh-CN.md)
和 [docs/oobe.zh-CN.md](docs/oobe.zh-CN.md)。

## 同级目录

压缩包里有四个顶层目录，不是一个：

| 目录 | 是什么 |
|------|--------|
| `NXSOL/` | 本目录 —— 能启动的那个系统 |
| `NX-GUI-Spec/` | 桌面蓝图：NXDisplay、NXShell、NXDock、NXMenu、NXExplorer；IPC 协议；线框图 |
| `NX-Ports/` | 移植笔记：`.deb` 兼容、nxpkg |
| `NXS-archive/` | 自研内核，归档但不是删除 |

GUI 规范之所以是同级而不是子目录：它不在构建链里（这里没有任何东西编译它），但它
也不是归档 —— 它是桌面里程碑照着写的那份文档。把一份还在用的蓝图塞进名叫 "archive"
的目录里是分类错误，已经改了。

## PATH

Linux 本身就有 `PATH`，NX 沿用不改。只额外加一个目录：`/nx/bin` 放 NX 组件，且排在 `apt` 所拥有的 `/usr/bin` 之前。两个前缀永不互相覆盖。

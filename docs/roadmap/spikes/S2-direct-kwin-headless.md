# S2 直连 KWin headless（VM 部分）

状态：**VM 部分完成**。HDR 的真机结论、延迟和 Prism 采集链路（`kwingrab.cpp`）没有测。

## 目的

验证"不要外层合成器，直接用 `kwin_wayland --virtual` 运行 Plasma"的可行性，对比 S1 的嵌套结构，为 ADR-0005 提供数据。

## 环境

T0.3 的 VM，Fedora 44，Plasma/KWin 6.7.5，SELinux Enforcing，无 GPU。在 S3 的无座位 PAM 会话（用户
`s3user`）里运行，使用用户的真实会话总线（不用 `dbus-run-session`）。

## 步骤

1. 在用户管理器的 `PATH` 前放一个 `kwin_wayland` 垫片，把 `--virtual --width W --height H` 追加到参数后
   （`startplasma-wayland` 不转发这些参数，与 S1 的 F1 相同）。
2. `systemd-run --user ... startplasma-wayland`，不设 `WAYLAND_DISPLAY`。
3. 用 `spectacle`（KWin ScreenShot2）截图，`kscreen-doctor` 查询和修改输出，用 `libei`（ctypes）连接 KWin 的
   EIS 注入输入。

## 结果

| 项 | 结果 |
|---|---|
| Plasma 在 `--virtual` 上运行 | **通过**：面板、托盘、Dolphin、Firefox 图标、文件管理器等与本地桌面一致；KWin 在 `wayland-0` 上监听，Xwayland 就绪 |
| 输出 | `Virtual-0`，初始 1920×1080@60 |
| 改分辨率 | **通过**：`kscreen-doctor output.Virtual-0.addCustomMode.<w>.<h>.<mHz>.full` 加自定义模式，再 `output.Virtual-0.mode.<w>x<h>@<r>` 切换：1280×720、2560×1440、**3840×2160@120** 均成功，Plasma 即时跟随，没有重启。自定义模式的实际刷新率略有偏差（59.85、59.96、119.98 Hz），且每次添加都会在模式列表里累积一个新模式 |
| HDR | **incapable**（`HDR`、`Wide Color Gamut`、`Vrr`、`ICC profile`、亮度控制都是 incapable/unsupported）：这个版本的虚拟后端没有 HDR |
| 截图 | 通过（ScreenShot2 对 `spectacle` 开放） |
| 采集协议 | `zkde_screencast`、`org_kde_kwin_fake_input` 不出现在未授权客户端的全局列表里，需要 `X-KDE-Wayland-Interfaces` 授权（Prism 的 `kwingrab.cpp` 已经处理）；采集本身没有测 |
| EIS 输入 | **通过**：`org.kde.KWin.EIS.RemoteDesktop.connectToEIS` 对普通会话客户端开放（不需要授权）；libei 看到三个设备：`eis absolute device`（abs、scroll、button）、`eis pointer`（相对 pointer、scroll、button）、`eis keyboard`。绝对指针点击选中了 Dolphin 里的文件夹，Ctrl+L 加输入 `eis-ok-12345` 在地址栏里完全正确 |
| 游戏需要的协议 | 未授权客户端可见 `zwp_pointer_constraints_v1`、`zwp_relative_pointer_manager_v1`、`zwp_keyboard_shortcuts_inhibit_manager_v1`、`wp_color_manager_v1`、`frog_color_management_factory_v1`、`wp_tearing_control_manager_v1` |
| polkit 认证框 | 与嵌套结构相同：开机弹出网络控制的管理员密码框（S3 的 Q6） |

## 与 S1（嵌套）的对比

| 维度 | 嵌套（S1） | 直连 KWin（S2） |
|---|---|---|
| 桌面可运行 | 通过，需要给内层 KWin 加全屏参数的垫片 | 通过，需要同样的垫片 |
| 分辨率跟随 | 通过（改外层输出，内层自动跟随，外层不停服） | 通过（`kscreen-doctor` 改模式，刷新率有小数偏差，模式列表累积） |
| 键盘、鼠标 | 现有输入桥 → labwc 虚拟设备，通过；需要外层不抢快捷键（外层缺 `keyboard-shortcuts-inhibit`） | EIS 通过，需要为键盘、绝对/相对指针各写一套转换，Prism 现有输入桥不能复用 |
| 快捷键抑制 | 外层缺协议，只能靠外层不绑快捷键 | KWin 自己支持 |
| HDR | 内层 incapable，原因是外层是不带色彩管理的发行版 labwc；`prism-labwc` 下待真机 | **虚拟后端 incapable**，没有 HDR 的路径 |
| 采集 | 沿用 `wlgrab.cpp`（已验证 screencopy 可抓到完整内层画面） | 沿用 `kwingrab.cpp`，本实验未测 |
| 桌面环境无关 | 是 | 否，只适用于 KWin |

## 结论

- 功能上两种结构都能工作。直连 KWin 的主要劣势是**虚拟后端在 6.7.5 上没有 HDR**，而 HDR 是 Prism 的核心卖点之一；
  嵌套结构的 HDR 取决于 S1 的真机结果（`prism-labwc` + 内层 KWin 的色彩管理协议传递）。
- 输入：EIS 完整可用（含独立的相对指针设备），所以如果最终选直连，输入没有技术障碍，只是要新写一套。
- 目前的证据不支持把直连 KWin 作为默认结构，但**不能否决**：如果 S1 在真机上发现 HDR 无法穿过两层，直连 KWin
  的 HDR 缺失会让两者都不满足 HDR。这时需要的是 KWin 上游的 HDR 虚拟输出支持，或游戏场景保留现有 Steam Headless
  （gamescope/labwc）路径，这与风险登记里的应对一致。

**ADR-0005 保持"提议"状态**，等待 S1 的真机数据（HDR、游戏输入、延迟）。

## 未覆盖

- 真机 HDR、延迟、GPU 占用；`kwingrab` 到编码的实际串流；KWin headless 的长时间稳定性（本实验约 10 分钟，没有崩溃）。
- 真机补测的共同前提见 `S1-nested-plasma.md` 的"真机补测记录"（R2 无图形会话时 Prism 无法启动、R3 无座位会话的设备权限、
  R4 NVENC 预设编号问题、R5 无头模式下没有可用的 GPU 编码路径）。这些对直连 KWin 同样适用；`kwingrab` 走 KWin
  ScreenCast，捕获后端自带 Vulkan 设备，所以 R5 的 Vulkan 设备选择问题可能不出现，但需要实测。
- 相对指针的游戏表现（EIS 的相对指针设备存在，但没有用游戏验证指针锁定）。

## 真机补测记录（2026-11-02，Kinoite + RTX 5080，KWin 6.7.5）

环境与 S1 真机补测相同（见 `S1-nested-plasma.md`：linger、属主 udev 规则、带 NVENC 修复的 Prism）。用户的本机 Plasma 是登录界面（`plasmalogin`），
S2 的桌面由 SSH 侧起在用户管理器里。

### R1 `kwin_wayland --virtual` 在真机上起 Plasma（通过）

做法：`~/s2/shim/kwin_wayland` 垫片追加 `--virtual --width 2560 --height 1600`；
`systemd-run --user --unit=s2-plasma -p UnsetEnvironment=WAYLAND_DISPLAY ... startplasma-wayland`（用户真实会话总线，不用 `dbus-run-session`，与 VM 一致）。
结果：KWin 监听 `wayland-1`，`plasmashell` 起来，输出 `Virtual-0`，2560×1600@60；`nvidia-smi` 里 `kwin_wayland` 占约 32 MiB 显存，说明走了 GPU。
`kscreen-doctor -o`：`HDR: incapable`、`Wide Color Gamut: incapable`、`Vrr: incapable`、`ICC profile: incapable`、`Allow EDR: unsupported`，与 VM 结果一致，
即**真机上虚拟后端同样没有 HDR**。（Prism 已有的 `Desktop (Virtual)` 是在真实 DRM 的 KWin 上加虚拟输出，日志里有 `enabling hdr/wcg on Virtual-Prism-Virtual`，
那是另一种结构，不是 S2；它需要本机登录的 Plasma，本次没有测。）

### R2 Prism 通过 `kwingrab` 抓取（通过，只测到连接 10 秒）

用户管理器 `WAYLAND_DISPLAY` 改为 `wayland-1` 后重启 `prism.service`：启动探测通过 KWin ScreenCast（`Screencasting with KWin ScreenCast`，PipeWire 节点创建成功），
`h264_nvenc`、`hevc_nvenc`、`av1_nvenc` 全部找到，`/serverinfo` 返回 200。没有再遇到 S1 里的 R2（无显示器）和 R5 问题。
`ScreenCast` 帧率报告为 `0/1 (variable rate capture)`。新增 Prism 应用 `S2 KWin Virtual`（`prism-capture: default`，镜像当前显示）。
用户在 00:36:17 到 00:36:27 连接了约 10 秒（AV1，SDR），**画面本身用户没有评价**，我没有测帧率或延迟。

### R3 Prism 的键鼠（uinput）到不了虚拟后端的 KWin，落到本机登录界面（现象确认）

用户反馈：鼠标键盘都控制了物理显示器。检查：虚拟后端的 `kwin_wayland`（pid 720331）没有打开任何 `/dev/input/*`（`/proc/<pid>/fd` 里 0 个），
而 `Mouse passthrough`、`Mouse passthrough (absolute)`、`Keyboard passthrough` 三个设备存在，所以事件被 seat0 上的登录界面合成器消费，
与 S1 的 R3 同一种失败方式（**输入泄漏到本地 greeter，而不是安静地失效**）。结论：Prism 现有的 inputtino 路径在直连 `--virtual` 的 KWin 上不能用，
**Prism 里没有 EIS 或 `fake_input` 的输入后端**（`src/` 里没有 libei 或 fake_input 的引用），要用直连就必须新写一套输入转发。
VM 里已验证 EIS 本身可用（含相对指针设备）；真机上没有测。

### 小结（真机）

- 直连 `--virtual`：桌面和 ScreenCast 采集可用，NVENC 全部通过；**HDR 没有，输入需要新写**。
- 没有测的：游戏、指针锁定（EIS 相对指针 + 游戏，这是与 S1 的 R10 对比的关键）、延迟、GPU 占用、长时间稳定性、手柄（要重新授权）。
- 每项都要先有输入通路才能测；这需要写代码（一个 EIS 转发桥），属于范围问题，等用户决定。
- 用户决定（2026-11-02）：S2 真机评估到此为止，不写 EIS 桥；指针锁定对比、延迟、性能和 HDR 都留到主干功能完成之后。
  HDR 的后续方向（给 KWin 嵌套后端打补丁等）记在 `S1-nested-plasma.md` 的 R16 末尾；`--virtual` 后端的补丁可行性更低。

# S1 嵌套 Plasma 运行在 prism-labwc 中（中期报告）

状态：**VM 部分完成，真机部分进行中**。VM 里能验证的功能项已全部通过（见 F7、F8）；HDR、游戏输入、延迟等
需要真机上的 `prism-labwc`，步骤见"真机验证步骤"。真机上外层合成器和 Prism 启动已打通，但 GPU 编码受阻（NVENC 预设
编号问题和无头模式下的 Vulkan 编码问题），见"真机补测记录"。

## 目的

验证"外层 headless labwc + 内层完整 Plasma"这种合成结构（ADR-0005 的嵌套方案）是否可行，并收集
Q1–Q4 的数据。T0.2 已搁置，所以本实验只记录绝对值，不与基线对比。

## 环境

| 项 | 值 |
|---|---|
| 位置 | T0.3 的 VM（`prism-e2e`，libvirt `qemu:///session`，4 vCPU，6 GiB，virtio-gpu，无硬件 GPU） |
| 客户机 | Fedora 44，内核 6.19.10，Plasma/KWin 6.7.5，labwc 0.9.6（发行版包，非 `prism-labwc`） |
| 渲染 | 外层 `WLR_RENDERER=pixman` |
| 用户 | `prism`，登录前无本地图形会话（SDDM 停在登录界面），已开启 linger |

因为用的是发行版 labwc，本报告**不覆盖 HDR、Vulkan 和任何延迟或 GPU 数据**，这些需要真机上的
`prism-labwc`（见"待在真机验证"）。

## 步骤

1. 外层：`systemd-run --user --unit=s1-outer` 运行
   `WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 labwc --config-dir <dir>`，随后
   `wlr-randr --output HEADLESS-1 --custom-mode 1920x1080@60`。
2. 内层：`systemd-run --user --unit=s1-inner` 运行
   `WAYLAND_DISPLAY=wayland-0 dbus-run-session startplasma-wayland`。
3. 用 `grim` 从外层截图。

## 发现

### F1 内层 KWin 默认是外层里的一个普通窗口

KWin 检测到 `WAYLAND_DISPLAY` 后自动选 Wayland 后端，开成 1024×768 的带标题栏窗口（标题
"Press right control key to grab pointer"），不会铺满外层输出。

`kwin_wayland` 有 `--width`、`--height`、`--fullscreen true`、`--scale`、`--output-count`，可以解决。
但 `startplasma-wayland` 通过 `kwin_wayland_wrapper` 启动 KWin，不转发这些参数。可行办法：在用户管理器的
`PATH` 前面放一个 `kwin_wayland` 垫片（`systemctl --user set-environment PATH=...`），把参数追加后
`exec /usr/bin/kwin_wayland`。垫片后内层铺满 1920×1080，`kscreen-doctor -o` 显示输出
`WL-0 1920x1080@60`。

结论：T2.3 需要一个稳定的办法把输出尺寸和全屏传给内层 KWin（垫片、`KWIN_*` 环境变量或自写启动
序列，需评估；不要依赖 PATH 垫片这种脆弱手段）。

### F2 外层协议集（stock labwc 0.9.6）

有：`zwp_relative_pointer_manager_v1`、`zwp_pointer_constraints_v1`、`zwlr_virtual_pointer_manager_v1`、
`zwp_virtual_keyboard_manager_v1`、`zwp_input_method_manager_v2`、`zwp_text_input_manager_v3`、
`zwlr_foreign_toplevel_manager_v1`、`zwlr_screencopy_manager_v1`、`ext_image_copy_capture_manager_v1`、
`wp_cursor_shape_manager_v1`、`wp_fractional_scale_manager_v1`、`wp_presentation`。

缺：**`zwp_keyboard_shortcuts_inhibit_manager_v1`**（KWin 启动时明确报告
"isn't supported by the host compositor"）、`wp_color_manager_v1`（stock 版没有，`prism-labwc` 靠 wlroots
补丁提供，需真机确认）。

对 Q3 的含义：相对指针和指针锁定的协议在外层是齐的；快捷键抑制缺失，所以外层必须不绑定任何
会抢键的快捷键（Super、Alt+Tab 等）。外层 `rc.xml` 里放一个无动作的占位 keybind
`<keybind key="W-F24"><action name="None"/></keybind>` 来避免加载默认快捷键，是否确实生效待用 `wtype`
验证。

### F3 内层 KWin 的 HDR 为 incapable

`kscreen-doctor -o`：`HDR: incapable`、`Wide Color Gamut: incapable`、`Vrr: incapable`。外层是 stock
labwc，没有色彩管理协议，这是预期结果，**不能据此判断 Q2**。要用 `prism-labwc`（wlroots 色彩管理
补丁 + Vulkan）在真机复测。

### F4 Plasma 假定自己拥有用户管理器

`startplasma-wayland` 在用户的真实 systemd 用户管理器里启动 `plasma-*` 单元；会话结束时激活
`exit.target`，停掉用户管理器里的一切（PipeWire、WirePlumber、D-Bus 代理单元，也包括同一管理器
里的外层 `s1-outer`）。这与 ADR-0003"远程会话是用户唯一的会话"一致，但意味着：

- 内层 Plasma 退出会连带结束外层和同会话的所有单元，T2.3 的会话单元必须接受这一点（把"内层桌面
  退出"当作会话结束，对应架构 3.7 的 `ending`）。
- 嵌套测试不能和用户的另一个活动会话共用一个用户管理器，否则内层退出时会破坏对方。S1 在 VM 中
  做正是为此。

### F5 用户管理器的存续取决于是否有会话或 linger

未开 linger 时，最后一个 SSH 会话断开 10 秒后（`UserStopDelaySec`）logind 停掉 `user@UID`，
触发 `exit.target`，把嵌套栈一起带走。前两次"Plasma 自行退出"都是这个原因，与 Plasma 无关。
开启 linger 后栈稳定运行。这印证了 M2 文档里"开启 linger"的前提，也说明 M3 必须由 PAM 会话
（或 linger）托住用户管理器。

### F6 VM 环境问题：SELinux 拒绝 polkit，UPower 起不来，plasmashell 卡死

内层 `plasmashell` 进程存在但主线程卡死：`evaluateScript` 超时；日志每 25 秒一条
`Failed to fetch all devices ... NoReply`；画面只有壁纸，没有面板。原因链：

1. VM 里 `upower.service` 起不来（`status=203/EXEC`）。
2. 手动运行 `upowerd` 也中止：
   `failed to get polkit authority: ... AccessDenied: Sender is not authorized to send message`。
3. 系统日志里有对应的 SELinux 拒绝（Enforcing）：`policykit_t` 向 `unconfined_t`（`ssh` 登录的
   `prism` 用户进程）的 D-Bus `send_msg` 被拒。

处理（仅 VM，运行时，重启后恢复）：`sudo setenforce 0`，并用 `sudo systemd-run --unit=upowerd-s1b
/usr/libexec/upowerd` 在沙箱外运行 `upowerd`（`upower.service` 即使 permissive 下仍是 203，原因未查）。
之后 `plasmashell` 不再卡死，完整桌面出现（面板、启动器、托盘、时钟）。

结论：这是 VM 镜像的环境问题，不是嵌套结构的问题；但它说明 **无人登录的远程会话要在 SELinux
Enforcing 下工作，会话进程必须运行在正确的 SELinux 域里**，这是 M3 的 PAM 会话要面对的问题，归入 S3 的
验证点。T0.3 的 VM 脚本应在 provision 中固化这个处理（或定位根因后修正）。

### F7 功能验证结果（VM，stock labwc 0.9.6，1080p）

| 项 | 结果 | 说明 |
|---|---|---|
| 内层 Plasma 在外层里启动 | 通过 | 面板、启动器（Kickoff 图标）、Dolphin、Firefox 图标、托盘、时钟齐全 |
| 铺满外层输出 | 通过（需 F1 的垫片） | `kscreen-doctor` 显示 `WL-0 1920x1080@60` |
| 外层改分辨率，内层跟随 | **通过** | 1920×1080 → 1280×720 → 2560×1440 → 1920×1080，内层输出几何每次都在 4 秒内跟到，无需重启 |
| 文件对话框出现在串流画面 | **通过** | 经 portal `FileChooser.OpenFile` 打开的 KDE 对话框出现在外层输出里（P3 的结构性问题解决）；调用方进程退出时 portal 会关掉对话框，属正常行为 |
| 多窗口与任务栏 | 通过 | 两个 Dolphin 窗口堆叠正常，任务栏显示 |
| 键盘输入穿透 | **通过**（经 Prism 输入桥） | Ctrl+L 后输入 `keyboard-ok-12345` 在 Dolphin 地址栏里完全正确。`wtype` 因每个字符替换 keymap 而乱码，不能用于此类测试 |
| Super、Alt+Tab 是否被外层截获 | **通过** | Super 打开内层 Kickoff；按住 Alt 再按 Tab 出现 KWin 的任务切换器。外层 `rc.xml` 需要有一个无动作的占位 keybind（`W-F24`）来避免加载默认快捷键 |
| 鼠标相对移动与悬停 | **通过** | 经输入桥的相对移动把光标移到侧边栏"Documents"上，高亮正确 |
| 光标显示 | **通过** | `grim -c`（带光标的 screencopy）里只有一个箭头光标，没有双光标 |
| Xwayland 应用 | **通过** | 内层 KWin 的 Xwayland 里运行 `xterm`，带 KWin 窗口装饰，`_NET_SUPPORTING_WM_CHECK` 存在 |
| 用 Prism 串流外层输出 | **VM 做不到** | 见 F8；外层输出用 `grim`（wlr-screencopy，与 `wlgrab.cpp` 同一协议）可以抓到完整内层画面 |
| 相对指针、指针锁定 | 仅确认协议 | 外层有 `zwp_relative_pointer_manager_v1`、`zwp_pointer_constraints_v1`，内层 KWin 也提供；真正的锁定要用 FPS 游戏在真机验证 |

### F8 Prism 的 headless 启动在 VM 里跑不通，原因是 DMA-BUF，不是嵌套

`prism-headless-start.sh` 要求外层 labwc 提供输出、DMA-BUF、screencopy 和虚拟输入协议。VM 的 virtio-gpu 没有 3D，
Mesa 走 `kms_swrast`，需要在 `/dev/dri/card1` 上创建 dumb buffer，但 `prism` 不是 DRM master，报
`DRM_IOCTL_MODE_CREATE_DUMB failed: Permission denied`，labwc 起不来 `HEADLESS-1`（用 `PRISM_RENDER_DEVICE`
指到 `renderD128` 也一样）。所以 VM 无法做"Prism 串流外层"，只能用 pixman 外层加手动接上 Prism 的输入桥。
NVIDIA 真机走 EGL/Vulkan 渲染节点，不受此限。

### F9 无座位会话里设备权限靠 `uaccess` 的假设不成立

`61-prism-input.rules` 用 `TAG+="uaccess"` 给会话用户授予 Prism evdev 节点的访问权；`/dev/dri/card*` 也靠
`uaccess`。`uaccess` 只授权给**座位上的活动会话**。无座位的远程会话（M3 的 PAM 会话，也包括这次的 ssh
加 linger）拿不到这些权限，`prism-input-bridge` 打不开 `/dev/input/event*`，合成器也无法使用 DRM 设备。
VM 里用 `sudo setfacl -m u:prism:rw` 临时授权。这与 S3 的验证点（`renderD*`、`/dev/nvidia*`、uinput）
直接相关，M3 需要为远程会话提供基于用户组或 ACL 的规则，不能依赖 `uaccess`。

### F10 输入桥的门槛

`prism-input-bridge` 只接受名称以 `Keyboard passthrough`/`Mouse passthrough` 开头，并且
vendor/product/version 为 `0xBEEF/0xDEAD/0x0111` 的 evdev 设备；且只有当 `PRISM_CAPTURE_OVERRIDE_FILE`
内容恰好是 `wlroots:<PRISM_SESSION_ID>\n` 时才独占并转发。测试用 uinput 脚本按此伪造设备即可，不需要 Prism
进程。

## 真机验证步骤

前提：先退出本地 KDE 会话（用 SSH 操作，或另开测试用户），`loginctl enable-linger <user>`，避免同一用户两个
Plasma。要用到 `prism-labwc`（`contrib/virtual-session/build-headless-compositor.sh`）。

1. 在 `~/.config/prism/apps.json` 里加一个应用：`"name": "S1 Plasma"`、`"prism-capture": "headless"`、
   `"cmd"` 指向下面的脚本、`"wait-all": true`。HDR 测试时在客户端打开 HDR，Prism 会自动用 `prism-labwc`。
2. 脚本（按客户端分辨率给内层 KWin 加参数，要求 `PATH` 垫片在用户管理器环境里）：

   ```bash
   #!/usr/bin/env bash
   set -eu
   W="${PRISM_CLIENT_WIDTH:-1920}"; H="${PRISM_CLIENT_HEIGHT:-1080}"
   mkdir -p ~/s1/shim
   printf '#!/bin/sh\nexec /usr/bin/kwin_wayland "$@" --width %s --height %s --fullscreen true\n' "$W" "$H" > ~/s1/shim/kwin_wayland
   chmod +x ~/s1/shim/kwin_wayland
   systemctl --user set-environment PATH="$HOME/s1/shim:/usr/local/bin:/usr/bin"
   exec dbus-run-session startplasma-wayland
   ```

3. 用 Moonlight 和 Iris 启动该应用，逐项记录：
   - HDR：内层 `kscreen-doctor -o`（需在内层会话的 D-Bus 下）的 `HDR`、`Wide Color Gamut` 是否变为
     capable；开启 HDR 的游戏或视频是否有 HDR 画面（Q2）。
   - 一个 FPS 游戏：相对指针、指针锁定、视角是否正常，Super/Alt+Tab 是否被外层吃掉（Q3）。
   - 光标：是否只有一个，形状和隐藏是否正确（Q4）。
   - 在客户端改分辨率或旋转后，内层是否跟随（本报告 VM 已验证外层改模式能跟随，真机需验证从客户端触发）。
   - 延迟与 GPU 占用的绝对值（`nvidia-smi dmon`、客户端统计叠加层）；对照同样条件下 Desktop Headless 的数值，
     以便评估多一层合成的代价。
4. 把结果追加到本报告并改写"给 ADR-0005 的暂时建议"。

## 真机补测记录（2026-11-01，进行中）

环境：Fedora 44 Kinoite（rpm-ostree），RTX 5080，驱动 615.78.08，SELinux Enforcing，已安装的 AppImage（用户级）。
本地 Plasma 已注销，通过 SSH 操作并开启 linger（`loginctl enable-linger`）。`prism-labwc` 在 Fedora 44 容器里
用 `build-headless-compositor.sh` 编译（`PRISM_INSTALL_PREFIX` 指向挂载目录），再装到 `~/.local/bin`；宿主与容器
同为 Fedora 44，`ldd` 无缺失库。不可变系统不需要 `rpm-ostree install`。

本节只记录到"外层合成器 + Prism 启动 + 编码器探测"为止，嵌套 Plasma、HDR、游戏输入、延迟都还没测。

### R1 外层合成器在 NVIDIA 上可以运行

`WLR_BACKENDS=headless`、`WLR_RENDERER=vulkan`、`hdr=true` 的 `prism-labwc` 0.20.1 起来了，Xwayland 持久化也正常。
`HEADLESS-1` 存在，协议齐全：`zwp_linux_dmabuf_v1`、`zwlr_screencopy_manager_v1`、`ext_image_copy_capture_manager_v1`、
`zwlr_virtual_pointer_manager_v1`、`zwlr_output_manager_v1`、`wp_color_manager_v1`、`zwp_relative_pointer_manager_v1`、
`zwp_pointer_constraints_v1`。日志里有一条 `gbm_bo_create_with_modifiers failed: Invalid argument`（为 `HEADLESS-1`
建无修饰符交换链时），输出仍可用，上游 issue #106 的报告者也看到同样的信息和同样的结论，暂按无害处理。

### R2 Prism 在没有任何图形会话时无法启动

没有本地图形会话时，Prism 启动即退出：`Unable to find display or encoder during startup`（`src/video.cpp`
的启动探测，`validate_encoder` 需要一个真实显示器去试编码）。三条捕获路径都失败：KMS 需要 `CAP_SYS_ADMIN`
（AppImage 拿不到）、portal 需要 D-Bus 会话、Wayland 没有可用显示。`prism.service` 进入重启循环。
先起一个无头 `prism-labwc`，再让用户管理器环境带上 `WAYLAND_DISPLAY`，Prism 就能启动：wlgrab 识别并选中
`HEADLESS-1`，探测约 20 秒完成，`/serverinfo` 返回 200。

含义：M2 的远程会话必须先于 Prism 的启动探测存在，或者探测需要能在没有显示器时延后进行。目前的路线图没有写这一点
（见"对问题清单的更新建议"）。

### R3 无座位会话的设备权限（F9 的真机确认）

注销本地会话后，`/dev/dri/card1` 的 ACL 转给了 `plasmalogin`（登录界面的用户），SSH 会话里的 `jhuang` 打不开，
Prism 报 `Couldn't open: /dev/dri/card1: Permission denied`。临时 `setfacl -m u:jhuang:rw /dev/dri/card1` 后
可以打开。`/dev/uinput` 同样没有授权给 `jhuang`：虚拟键盘、鼠标和全部手柄都是 `Permission denied`，所以输入类测试
（FPS 游戏、指针锁定）要先解决这一项。`renderD128` 是 `crw-rw-rw-`，不受影响，`/dev/nvidia*` 也是全员可读写。

### R4 NVENC 在这台机器上不可用（已有上游 issue）

探测日志：`Value 12.000000 for parameter 'preset' out of range [0 - 9]`，`Could not open codec [h264_nvenc]`。
这与本地 Plasma 在运行时（17:06）完全相同，不是本次环境造成的。原因见 atgehrhardt/prism issue #106：
`src/config.cpp:1461` 的 `video.nv_legacy.preset = video.nv.quality_preset + 11` 按旧版 FFmpeg 的编号（P1–P7 是
12–18）计算，新版 FFmpeg 重新编号后（P1 是 3，P7 是 9），`nvenc_preset` 被限制在 1–7，没有任何配置值能得到合法预设。
issue 报告者把预设改成 3 后，h264、hevc、av1 都通过探测，无头 HDR 下串流了 AV1 10-bit 4K。issue 目前没有维护者回复。
已安装的 AppImage 带这个问题，没有配置项可以绕过，所以在修复前，这台机器上的 NVENC 数据拿不到。

补充证据：
- 本地的 `~/Workspace/ffmpeg8/ffmpeg`（FFmpeg n8.1.2 静态构建，2026-06-30）用的是旧编号，`-preset` 范围 0–18、`p1` 是 12；
  Prism 自带的 FFmpeg（build-deps v2026.910.121303）是新编号 0–9。同一个整数在两个版本里含义不同，这正是问题所在。
- 用这个本地 ffmpeg 在本机的 SSH 会话（无座位、无本地图形会话）里直接编码：`h264_nvenc` 和 `av1_nvenc` 在 RTX 5080 上
  都成功，`-preset p1`、`p4`、`p7` 都有效。所以 NVENC 在无座位会话里本身可用，问题只在 Prism 传的预设值。
- 新编号下按名字传预设的行为没有在本机用新版 CLI 验证，依据是 issue #106 里报告者列出的 `p1`–`p7` 常量名；要等 Prism
  自带的 FFmpeg 实际跑一次编码探测来确认（见 R6）。

修复：分支 `fix/nvenc-preset-numbering`（提交 `b49fb419`，基于 `master`，已推送到 `fork` 远程，没有创建任何 issue 或 PR）。
不照 issue 建议的 `quality_preset + 2`，因为那样仍依赖数字编号，FFmpeg 下次再改编号会再坏。改为按名字传预设：
`nvenc::ffmpeg_preset_from_quality()`（`src/nvenc/nvenc_config.h`）返回 `p1` 到 `p7`（范围外截断），
`config::video.nv_legacy.preset` 从 `int` 改为 `std::string`，`src/video.cpp` 的三处选项按字符串指针取值，不需要改动。
用户可见的 `nvenc_preset`（1–7）不变，所以文档、`en.json` 和配置一致性测试都不受影响。
测试：新增 `tests/unit/test_nvenc_config.cpp`（5 个用例：1–7 映射、两端截断、默认值、av1/hevc/h264 三个编码器的
`preset` 选项是字符串指针并绑定到配置）。Fedora 44 容器里 `-Werror` 编译通过，完整测试集 429 个里 420 通过、
9 跳过（GPU 和硬件相关，与之前一致）、0 失败。`config.cpp` 里改动的那一行没有单独测试：`apply_config` 没有对外暴露，
而 `config::parse` 会在 `platf::appdata()` 下写文件，不适合放进单元测试；映射逻辑本身在头文件函数里，已全覆盖。
clang-format 和 doxygen 在容器里不可用（构建关了文档），按现有风格和格式手写，没有工具验证。

### R5 无头模式下 Vulkan 编码器也不可用（独立于 R4）

本地 Plasma 运行时，Vulkan 编码器通过了探测（`h264_vulkan`，捕获走 KWin ScreenCast，捕获后端自带 Vulkan 设备，
`encode_device->data` 非空，直接复用）。无头模式走 wlgrab，没有自带设备，退到 `src/video.cpp` 的
`vulkan_init_avcodec_hardware_input_buffer` 里"按路径创建，再按 `1, 0, 2, 3` 回退"的逻辑，结果：

1. `Unable to find device "/dev/dri/renderD128"`：按渲染节点路径匹配失败。宿主上的 `vulkaninfo`（含 AppImage 自带的
   `libvulkan.so.1`、在用户管理器上下文里运行）都能正确报告 NVIDIA 的 `renderMajor/Minor = 226/128`，所以不是
   驱动或 SELinux 的问题，原因没有查清（没有可用的独立 ffmpeg 来复现）。
2. 回退按索引 `1` 先试。单显卡机器上索引 1 是 llvmpipe，设备创建成功，但没有 `VK_KHR_video_encode_queue`，探测失败，
   并且不会再试索引 0。
3. 用 `VK_DRIVER_FILES` 隐藏 llvmpipe 后，回退命中了 NVIDIA（`Using Vulkan device index: 0`），但接着报
   `Couldn't scale frame: Invalid argument`，说明 wlgrab 的帧到 Vulkan 编码的转换也不通。

结论：探测最终落到软件编码（`libx264`），这次真机测到的任何延迟和 GPU 占用都没有参考价值。要得到有意义的数据，
需要先有可用的 GPU 编码路径，目前最可能的是修复 R4 后走 NVENC（issue #106 的报告者就是这样在无头模式下工作的）。

没有修的部分（只记录，不属于本任务，需要你决定是否处理）：
- 回退顺序 `"1", "0", "2", "3"` 在单显卡机器上先命中 llvmpipe；设备创建成功不代表它支持视频编码，回退应当检查
  `VK_KHR_video_encode_queue` 或优先选独立 GPU。
- 按渲染节点路径匹配 `/dev/dri/renderD128` 在无头模式下失败的原因没有查清。
- wlgrab 的帧到 Vulkan 编码的转换（`Couldn't scale frame`）不通。

### R6 带 NVENC 修复的 AppImage（构建与安装）

为了在真机上拿到有意义的数据，需要一个带 R4 修复的 Prism。已安装的 AppImage 基于 `docs/roadmap` 这条线（含 app-sync），
而修复在 `master` 上，所以：
- 临时 worktree `../prism-s1-build`，分支 `test/s1-nvenc-fix`：`docs/roadmap` 加上 cherry-pick 的修复（`59172b2d`），
  不推送，仅用于本次测试。
- 用官方流程构建：`podman build -f packaging/linux/AppImage/Dockerfile -t prism-appimage-build .`，再
  `podman run ... prism-appimage-build bash scripts/build-appimage.sh`（Ubuntu 24.04 基线，CUDA 打开，`PRISM_BUILD_JOBS=16`）。
  Fedora 容器里编出的二进制不能直接替换进已安装的 AppImage：安装的二进制自带 ICU 74 等库，`RUNPATH=$ORIGIN/../lib`。
- 构建踩到的坑：worktree 的 `.git` 文件指向 `/var/home/jhuang/Workspace/prism/.git/worktrees/<名字>`，打包阶段
  （`package-appimage-licenses.sh` 等）要在 `/src` 里跑 git，所以除了下节"三个坑"里的两个挂载，还要把主仓库的 `.git`
  只读挂到它的宿主原路径。第一次构建因此在最后一步失败，编译产物不受影响，补挂载后重跑只花了几分钟。
- 构建结果（2026-11-01）：`cmake-build-appimage/artifacts/prism-x86_64.AppImage`，56M，`.sha256` 校验通过；builder 镜像约
  12 分钟，AppImage 编译加打包约 6 分钟（20 核）。
- 安装：没有用 `install.sh`。它会用 `sudo` 重写 `/etc/udev/rules.d`、`/etc/modules-load.d` 并 `modprobe`（本机 10 月 26 日已
  装好），还会把 service 改成指向 `~/.local/share/prism/prism.AppImage`；而本机是解包安装（`~/.local/opt/prism`，
  service 直接指向 `AppRun`，`~/.local/opt/` 下还有上次的 `prism.bak-20261027-040315`）。这次沿用解包方式：
  `AppImage --appimage-extract`（静态 runtime，不需要 FUSE）解到 `~/.local/opt/prism.new`，`AppRun --check` 通过，
  停 `prism.service`，旧目录改名为 `~/.local/opt/prism.bak-20261101-184837`，新目录换成 `~/.local/opt/prism`，再启动。
  不需要 `sudo`。回滚：停服务，把备份目录改回 `prism`，再启动。
- 验证结果：NVENC 通过探测，`h264_nvenc`、`hevc_nvenc`、`av1_nvenc` 都找到（`Found H.264/HEVC/AV1 encoder ... [nvenc]`），日志里
  `out of range` 出现 0 次，探测不到 1 秒（之前落到 `libx264` 软件编码要约 20 秒）。av1 探测过程中有一条
  `Could not open codec [av1_nvenc]: Function not implemented`，最终仍找到 AV1。原因（已查日志）：这是 Prism 对 4:4:4 的
  探测失败（`YUV444P not supported`、`av1_nvenc: YUV 4:4:4 not supported`），日志里紧跟着有 "Ignore any errors mentioned
  above, they are not relevant"；RTX 5080 的 AV1 NVENC 不支持 4:4:4，4:2:0 的 AV1 不受影响。这同时确认了
  Prism 自带的新编号 FFmpeg 接受按名字传预设（R4 里"没在本机验证"的那一项）。
- 没有验证到的：这只是 SDR 的编码器探测。HDR 编码（10-bit、`prism-labwc` 的 HDR 路径）、实际串流、延迟和 GPU 占用都还没测。

### R7 输入设备授权（uinput、uhid）

`sudo setfacl -m u:jhuang:rw /dev/uinput /dev/uhid` 之后（两个设备原本只有 `plasmalogin` 的 ACL），重启 Prism：
键盘、鼠标、手柄的 `Permission denied` 消失；授权 `uinput` 前 `ds5`/`ds5-edge` 的禁用提示来自 `/dev/uhid`，
单独授权 `uinput` 不够，两个都要。这是 R3 在输入侧的确认：M3 的 helper 设计要同时覆盖 `card*`、`uinput`、`uhid`。
同一次启动日志里还有两条不阻塞的噪声：portal 捕获路径 `Failed to create session:`（R2 已知，最终选中 wlgrab）；
`kscreen-doctor ... Invalid config.` 约每 10 秒一次——Prism 的显示设备管理在 labwc 这类非 KDE 合成器上仍在调用
`kscreen-doctor`，对串流无影响，但值得在 T2.x 里考虑按合成器跳过。

### R8 真机串流嵌套 Plasma（SDR，首次打通，2026-11-01 19:04）

条件：Prism（带 NVENC 修复）、`prism-labwc`（Prism 自己起的私有会话 `wayland-1`）、内层 `startplasma-wayland`、本地 Plasma
已注销、SSH + linger；客户端 Moonlight（Android 配对设备），SDR，1080p60。日志：选用 `hevc_nvenc`，码率约 15 Mbps，
最低帧率目标 30 fps；`CLIENT CONNECTED` 到 `DISCONNECTED` 约 3 分 35 秒。

**打通前的两个阻塞（都是权限或环境，不是嵌套结构本身）：**

1. 键鼠落到本机物理屏幕的登录界面：`Keyboard/Mouse passthrough` 的 evdev 节点只有 `plasmalogin` 的 ACL，输入桥打不开
   （日志只有 "activated evdev forwarding"，没有 "opened ... passthrough"），设备挂在 seat0，被本地 greeter 当普通键鼠消费。
   临时 udev 规则 `/run/udev/rules.d/62-prism-s1.rules`（`SUBSYSTEM=="input", KERNEL=="event*",
   ATTRS{name}=="*passthrough*", OWNER="jhuang", MODE="0660"`，需要 `sudo`，重启失效）后，输入桥打开了 event3/4/21，键鼠
   在串流里生效。这是 F9 的第三个设备类别（`card*`、`uinput`/`uhid`、input 节点），也说明 `61-prism-input.rules` 的 `uaccess`
   对无座位会话不够；注意**失败方式是输入泄漏到本地 greeter**，而不是安静地失效，M3 需要把它当作安全相关的点。
2. 内层 KWin 没铺满，是 1024×768 的窗口（F1 在真机复现）：`kwin_wayland_wrapper` 用启动它的进程的 `PATH`（Prism 应用单元，
   AppImage 路径在前）去解析 `kwin_wayland`，不是用户管理器的 `PATH`，所以只改 `systemctl --user set-environment PATH`
   的垫片不生效；脚本里同时 `export PATH` 后铺满 1920×1080。F1 的结论更新：垫片要放进启动 `startplasma-wayland` 的那个
   进程的 `PATH`，只改用户管理器不够，这也印证了 F1 里"不要依赖 PATH 垫片这种脆弱手段"的判断。

**通过：** 内层 Plasma 桌面显示并铺满；鼠标键盘有效；音频正常（用户反馈）；Chrome 可用（用户反馈）。

**两个弹窗（无 PAM 会话的后果，对 M3 有直接意义）：**
- 一启动就弹 "System policy prevents control of network connections"，要输入密码。polkit 日志：
  `org.freedesktop.NetworkManager.network-control` 的授权对 `kded6`、`plasma-*` 进程失败（19:01、19:04）。内层 Plasma 的进程
  由用户管理器和 Prism 应用单元启动，不属于任何 logind 本地活动会话，所以 `allow_active` 类策略对它不成立。
- 打开 Chrome 时弹 KWallet 开启申请要输入密码：没有经过 PAM 登录，`pam_kwallet` 没有机会用登录密码自动解锁钱包
  （日志有 `ksecretd: Using kwallet without parent window!`）。
- 两项都是手动输入密码后恢复正常。这与 F6（SELinux 域）、F9 同属"无人登录的远程会话要有 PAM 会话"那一类问题：M3 要么走
  PAM 会话（解锁钱包、归属 logind 会话），要么明确接受这些提示并在文档里写明。推测，尚未用对照实验证实（例如在 PAM 会话里
  跑同样的栈是否消失）。

**其他日志观察（不阻塞）：**
- 每次启动探测前有 4 次 `Headless HDR requested but the output is not BT.2020/PQ`，而 `PRISM_CLIENT_HDR=false`；
  `/run/user/1000/prism-headless-hdr` 内容为 `1 203 1000`，原因没有查。HDR 测试时再看。
- `setpriority failed for nice -10/-15: Permission denied` 和 `EGL context priority ... CAP_SYS_NICE`：用户态没有提权能力，
  可能影响延迟，延迟数据要带上这一条。

**同一会话的后续用户反馈（SDR，2026-11-01）：**

| 项 | 结果 | 说明 |
|---|---|---|
| Super、Alt+Tab 到达内层 | **通过** | 客户端打开"捕获系统快捷键"后到达内层；外层没有截获（Q3 的快捷键部分） |
| 光标 | **通过** | 串流里只有一个光标（Q4 的双光标部分；形状与隐藏未专门测） |
| 客户端改分辨率 | **通过**（用户反馈） | 内层是否逐次精确跟随、耗时多少没有记录，需要时用 `kscreen-doctor -o` 复核 |
| 内层 Plasma 的显示缩放 | **从外层设置有效；内层自己的缩放设置无效** | 客户端 2560×1600@120，串流中用 `wlr-randr --output HEADLESS-1 --scale 1.5` 改**外层**输出：用户目测整个桌面清晰地放大到约 1.5 倍（文字和图标锐利，面板仍在屏幕底边，布局完整，不是图像拉伸或裁切），还原后恢复，过程完整可见（2026-11-01 19:57:37 到 19:57:57，plasmashell 在这两个时刻各有一次界面重排日志）。但同时内层 `kscreen-doctor -o` 一直显示 `Scale: 1`、几何 2559×1599（1.5 期间）/2560×1600（还原后），即**内层读数不反映实际生效的缩放**。在内层系统设置里改缩放、或用 `kscreen-doctor output.WL-0.scale.1.5`，返回码 0 无报错但没有任何效果（19:55 复现）。结论：嵌套结构里 HiDPI 缩放由外层 `prism-labwc` 的输出缩放控制，内层 Plasma 会跟随；T5.1 在外层设置即可，大概率不需要垫片 `--scale`。未验证：是不是所有 Qt/GTK/Xwayland 应用都清晰、1.25/1.75 等分数值、`wlgrab` 抓取的是物理像素（本次看到画面完整，说明是）、客户端侧缩放后的鼠标坐标是否正确 |

**缩放观察的更正：** 上一版结论"内层缩放既不能自己改，也不会从外层继承"是错的。用户第一次看到的变化是真实的（见上表），我当时只读了
`kscreen-doctor`，它在嵌套模式下读数不可信。教训：嵌套结构里不能只靠内层 `kscreen-doctor` 判断缩放，需要以串流画面为准；
所以 T5.1 的验收也要有画面确认，不能只检查输出配置。

**刷新率：内层固定 60Hz，与外层 120Hz 和客户端请求的 120fps 不一致。** 客户端 2560×1600@120：外层 `HEADLESS-1` 是 120Hz，
Prism 日志 `Requested frame rate [120fps]`，但内层 Plasma 设置里刷新率显示 60，`kscreen-doctor` 的模式是 `2560x1600@60.00`。
`kwin_wayland --help` 的窗口模式选项只有 `--width/--height/--scale/--output-count/--fullscreen`，没有刷新率。含义：嵌套结构下
内层应用最多按 60Hz 出帧，客户端的高刷新率设置在 S1 里是否有意义要靠实测帧率确认（推测，未测）；这影响 ADR-0005 对"嵌套"与
"直连 KWin headless"（S2）的比较，S2 的输出刷新率可以直接设置，要一起对照。

**共享 `~/.config` 的副作用（本轮顺带发现）：** 内层 Plasma 和本地 Plasma 用同一个用户的 `~/.config`，`kwinoutputconfig.json` 里现在同时有
本地物理屏的 `DP-1`（scale 1.15）、`HDMI-A-1`（scale 1.45）和内层的 `WL-0`，`setups` 有 4 条；文件在串流结束后（19:14）还被改写过，
是谁写的没有查。内层会话会往用户的真实显示配置里写条目，这与 F4（Plasma 假定自己拥有用户管理器）同类，ADR-0003 的"远程会话是用户
唯一的会话"成立时无害，但 S4（本地登录与远程并存）要验证两个会话互相写配置是否会破坏对方。另：内层 kwin 启动时日志有
`GL_OUT_OF_MEMORY ... Failed to allocate memory for texture` 和 `Invalid framebuffer status`，之后画面正常，原因没查。

**还没有的数据（下一轮）：** 分数缩放值和各类应用的缩放表现、FPS 游戏的相对指针与指针锁定、HDR、延迟和 GPU 占用的绝对值。

**Steam 在每次重建内层会话时重复下载/处理着色器预缓存（用户观察，20:25 查日志）：** `shader_log.txt` 里两个 Vulkan 管线桶
（`VulkanPipelinesV6_3554f158047e28bf` 1.38GB、`..._923fa87291dfec1c` 801MB）的键在 19:23、20:02、20:18 三次会话里完全相同，
所以不是 GPU/驱动指纹在嵌套会话里变了；3554 桶每次 Steam 启动都用同一个 manifest ID 重新下载，923fa 桶每次以
`Content unavailable` 结束（4 次），像是 Steam 侧原因导致永远算"没装好"，每次启动都重试。游戏库在 NTFS 上
（`/run/media/jhuang/ntfs_storage`，`ntfs3`，`case_sensitive`），是否与此有关没有验证。没有证据表明与 Prism 或嵌套结构有关；
对测试的影响是每次重建内层会话都要多等几分钟，所以后面尽量不重建会话。可选缓解（改的是用户全局 Steam 设置，由用户决定）：
Steam 设置里关掉"启用着色器预缓存"。

### R9 `uaccess` 重置会冲掉手动 ACL（本地登录界面重建时）

19:50:23 本地登录界面（plasmalogin greeter）会话重建（`c2` 变 `c3`），logind 在 seat0 活动会话变化时重新套用 `uaccess`，
`/dev/uinput`、`/dev/uhid`、`/dev/dri/card1` 上手动加的 `jhuang` ACL 全部消失（机器没有重启）。重启 Prism 后退到软件编码
（`libx264`），虚拟键鼠和手柄全部 `Permission denied`。处理：运行时 udev 规则里把这三个设备的**属主**设为 `jhuang`
（`OWNER` 不会被 `uaccess` 重置），`udevadm trigger` 后恢复，NVENC 三个编码器重新可用。含义：M3 的授权方式不能是一次性的
`setfacl`，也要能经受 greeter 重启和 seat0 会话切换；这是 F9 的第四条证据，属主或用户组规则比 ACL 稳。

### R10 游戏里指针被锁定后光标不动、点击正常（2026-11-01 20:50，现象与链路已定位，修复未做）

现象（Ready Or Not，Proton，经 Xwayland）：在游戏界面点击后光标立刻不动，点中菜单按钮时有概率不锁，Alt+Tab 切出后立刻恢复，切回有概率再锁。

方法：给内层 KWin 的启动 shim 加 `WAYLAND_DEBUG=client`（`~/s1/kwin-wl.log`，过滤指针约束相关行，`relative_motion` 每 100 条采 1 条）。
注意：Xwayland 是 KWin 的子进程，继承了该环境变量，日志里两条连接混在一起：墙钟时间戳 `[04:17:20.76]` 的是内层 KWin 对**外层 labwc**
的连接，单调时间戳 `[1813311.357]` 的是 Xwayland 对**内层 KWin**的连接。（我最初把两者混了，得出过"客户端发绝对坐标"和"KWin 向外层
发了 lock_pointer"两个错误结论，均已撤回。）

事实：
(1) 输入源头正常：停掉输入桥、直读 `Mouse passthrough` 设备 30 秒，`REL_X` 3541、`REL_Y` 2721、`BTN_LEFT` 4 次，`ABS` 为 0。客户端发的是
相对位移，Moonlight 的"为远程桌面优化鼠标"未勾选，与此一致。
(2) 内层：Xwayland 对内层 KWin 发了 76 次 `lock_pointer`、9 次 `confine_pointer`，内层 KWin 都回了 `locked`（内层约束成立）。
(3) 外层：内层 KWin 在 20:17:20 绑定了外层 labwc 的 `zwp_relative_pointer_manager_v1` 和 `zwp_pointer_constraints_v1`，
**之后整个日志里没有一次** 对外层的 `lock_pointer`、`confine_pointer` 或 `get_relative_pointer`（请求会被记录，格式与 bind 相同）。
(4) 日志里唯一一条 `relative_motion` 是内层 KWin 发给 Xwayland 的，dx/dy 全为 0。
(5) KWin v6.7.5 源码（`src/backends/wayland/wayland_backend.cpp`、`wayland_output.cpp`）：嵌套后端对外层的指针锁定只有一个触发点，
即键盘事件 `KEY_RIGHTCTRL` 按下时的 `WaylandBackend::togglePointerLock()`（成功后才创建外层 `relative_pointer`）；没有任何代码把内层
客户端的 `lock_pointer` 请求映射到外层。这是 KWin 嵌套后端为手动调试设计的行为，与日志 (3) 吻合。
结论（由 (1)-(5) 推出）：内层约束成立后，嵌套 KWin 6.7.5 既不把锁定转发给外层，也没有从外层取相对位移，
所以锁定中的内层客户端（游戏）拿到的相对位移恒为 0；按键不经这条路径，所以"光标冻住但能点击"；Alt+Tab 切走后内层约束解除，
光标恢复。点中菜单按钮时是否锁定取决于游戏自己是否请求锁定。
含义：指针锁定的游戏（FPS 的鼠标视角同理）在"外层 labwc + 内层嵌套 KWin"这个结构下默认不可用，这是对 Q3 的直接回答，
也是 ADR-0005（嵌套 Plasma）的一项实测缺陷证据。可能的方向（只列出，是否采纳、是否改计划由用户决定）：查 KWin 嵌套后端
是否有已知问题或更高版本修复；S2 路线（`kwin_wayland --virtual` 直接作为唯一合成器，输入桥直连）不经过嵌套，需要对照验证；
在外层 labwc 或输入桥一侧补偿；手动绕过办法是在内层会话里按右 Ctrl 切换外层锁定（已实测，见下）。还没做的验证：另一个会请求指针锁定的 Wayland 原生客户端是否同样失败（排除只是 Xwayland/Proton 的问题）。
附带观察：内层 KWin 的 `locked`/`unlocked` 在几百毫秒内会抖动 2-3 次，原因没查。

**右 Ctrl 绕过办法实测（20:50）：** 用户在光标被冻住时按一次右 Ctrl，光标立刻恢复；此后切屏（Alt+Tab 往返）和游戏内点击都不再冻住。
日志佐证：按下后内层 KWin 才创建对外层的 `relative_pointer`（对象 id 变为 5 位数），外层 `relative_motion` 持续到达
（采样后 200 多条，之前整段只有 1 条零值），内层 KWin 转给 Xwayland 的 `relative_motion` 也变成非零（缩放 1.5 时内层 delta 约为外层 unaccel 的 1.5 倍，
含义没细查）。尚未验证：进入关卡后的鼠标视角；外层处于锁定态时绝对坐标模式（"为远程桌面优化鼠标"勾选）是否可用，推测不可用，因为外层
labwc 锁定时会丢弃绝对坐标；这个切换是会话级、默认关闭的状态，每个新会话都要按一次，Prism 目前没有任何组件在做这件事。

### R11 载入游戏关卡时整机冻死，需强制重启（2026-11-01 20:52，原因未知）

现象：嵌套 Plasma 会话里游戏在载入一局时，SSH 和物理屏全部无响应，用户强制重启。上一次启动的最后一条日志在 20:52:51，
前 26 秒（20:52:25）是客户端重新连接（先 1280x720、随后 2560x1600，输出缩放 1.5，AV1 编码，34.9 Mbps），Prism 日志里没有错误，
之后只有无关的重复日志。冻死前用户刚验证过右 Ctrl 解除指针冻结（R10）。

能确认的：内存没有问题（31 GiB 内存、62 GiB swap，日志里无 OOM 记录）；`journalctl -b -1 -k` 对当前用户返回空（用户只在 `wheel` 组，
不在 `systemd-journal` 或 `adm`），`/var/lib/systemd/pstore/` 为空，所以**没有看到内核日志**，不能判断是否有 NVIDIA Xid 或内核报错。
不能确认的：根因。我没有证据把它归到 Prism、嵌套 KWin、NVIDIA 驱动、Steam 着色器处理（Fossilize 重放）或游戏本身中的任何一个。
同期的两个干扰项，均与测试无关：(a) 上一次启动从第一秒起 `obexd` 以约 230 次/秒反复失败（`obex_server_init failed`，38 分钟共约 40 万条，
进程号被白白烧掉约 85 万个），本次启动到目前为止是 0 条；(b) 用户的 `emby-local-player.service` 因脚本路径不存在重启了 1078 次。
内核日志核查（用户用 sudo 执行 `journalctl -b -1 -k` 和 `-g 'Xid|NVRM'`，均为 "No entries"）：全库 `_TRANSPORT=kernel` 共 1269 条，
全部属于**本次**启动，上一次启动一条内核消息都没有保留。原因有两个：(a) `obexd` 风暴把日志灌得太快（`SystemMaxUse=500M`，约 2 分钟写满一个 32 MB
文件），上一次启动 20:14:26 之前的内容（包括开机时的内核消息）已被轮转删除；(b) 20:14:26 到冻死这 38 分钟里没有任何内核消息落盘。
所以**没有 Xid、OOM、soft lockup 或 panic 的记录**；这与"内核或 GPU 驱动直接卡死、来不及写日志"一致，但也不能据此证明，硬件层面（供电、散热、
显存）同样排除不了。结论：下一次冻死必须靠测试前准备来留证据，见下。

**复现前的取证准备（建议，用户决定是否复现）：** 开机后先让日志不再被灌满（`obexd` 风暴本次为 0，需留意是否再出现）；复现时在后台持续写盘
`nvidia-smi dmon -s pucvmet -o T` 和温度/功耗采样（每秒一行并 `fsync`），冻死后能看到最后几秒的 GPU 功耗、温度、显存和时钟；
另一台机器可以 `ssh` 进来跑 `dmesg -w` / `journalctl -kf` 实时落盘（本机 SSH 本身也会死，但对端已收到的内容会保留）。
复现时先关 Steam 着色器预缓存，并分步加载（只开 Plasma → 再开 Steam → 再进关卡），确定是哪一步触发。

重启后丢失的临时状态（要重做）：`/run/udev/rules.d/62-prism-s1.rules`（输入设备属主规则，没有它 Prism 退回软件编码且虚拟键鼠无权限）、
`smoke-labwc` 单元和用户管理器环境里的 `WAYLAND_DISPLAY`（R2 的前置条件）、外层输出缩放（运行时设置）。linger 仍然开着。

**更新（23:46）：上面的干扰项 (a) 已被复现并定位，见 R12。** 本次启动的 `obexd` 失败不是 0：内层 Plasma 一启动（23:27:41）就开始，
约 330 次/秒，与冻死前的风暴同类。所以"风暴是冻死前的背景噪声"这一条要改成"每个嵌套 Plasma 会话都会产生"。是否与冻死有因果关系仍未证明。

### R12 每个嵌套 Plasma 会话都会触发 `obexd` 启动风暴（2026-11-01 23:46，已复现，成因已定位）

现象：内层 Plasma 启动的那一秒（23:27:41）起，用户日志里出现 `obexd[…]: OBEX daemon 5.87` 加 `obex_server_init failed`，
约 330 次/秒，连续 19 分钟累计约 19.5 万次（每分钟约 2 万条）。内层 `dbus-daemon` 常驻占约 23% CPU，客户端唯一名从 `:1.25` 涨到 `:1.189040`。
没有内层 Plasma 的时候（本次启动 20:56 到 23:27）为 0。

成因（D-Bus 抓包 4 秒确认）：内层会话里的 `kded6`（pid 15696，蓝牙模块）以约 170 次/秒的频率对**内层私有总线**调用
`StartServiceByName("org.bluez.obex")`；`/usr/share/dbus-1/services/org.bluez.obex.service` 的 `Exec=/usr/libexec/bluetooth/obexd`，
所以内层 `dbus-daemon` 每次都新起一个 `obexd`，它 `obex_server_init failed` 退出（**失败原因我没有查**；用户管理器里有一个 20:56 起运行的 `obex.service`，pid 2814，
可能是冲突方，但未验证），`kded6` 立刻再试。即"嵌套会话有自己的 `dbus-run-session` 私有总线，总线上缺少外层已经提供的服务名，
于是总线激活失败并被无限重试"。这不是 Prism 的代码问题，但**它是嵌套结构（内层私有总线与外层用户管理器共享单元）带来的副作用**：
每秒几百次 fork、同样数量的日志写入（日志 2 分钟轮转一个 32 MB 文件，冲掉了上次冻死前的所有内核日志，见 R11）。

含义：(1) 对 R11：冻死发生在 38 分钟持续风暴之后，这是**一个新的、可复现的嫌疑**，但"fork 风暴 + 日志写入"能否导致整机冻死没有证据，
也不能排除它只是伴随现象；(2) 对 ADR-0005：内层 Plasma 与外层用户管理器共享 `systemd --user` 和 `~/.config`，这类"两套总线重叠"的
问题还会有（本次已见 polkit、KWallet、`kscreen`，R7/F4/F5），属于嵌套结构的持续维护成本；(3) 对测试：风暴会冲掉日志，取证时要先压住它。
**缓解（23:55，已做，只改测试用的 `~/s1`，没有动 `~/.config`）：** 写了一个占着 `org.bluez.obex` 名字的桩进程 `~/s1/obex-stub.py`（Gio 的 `bus_own_name` 后空转），
并放了覆盖服务文件 `~/s1/dbus-override/dbus-1/services/org.bluez.obex.service`（`Exec` 指向桩），`~/s1/start-plasma.sh` 在 `dbus-run-session` 之前把 `~/s1/dbus-override`
放到 `XDG_DATA_DIRS` 最前面，所以以后新建的内层会话默认带它。**已验证的：** 对正在运行的内层总线单独起一个桩（`systemd-run --user --unit=s1-obex-stub`）后，
`obex_server_init failed` 在几秒内降到 0，内层 `dbus-daemon` CPU 从约 23% 降到约 0.5%，会话未受影响。**还没验证的：** 新建会话时覆盖服务文件是否被 `dbus-daemon`
优先采用（下次重建会话时看日志里是否仍有 `obexd`）。附注：我手动在内层总线上跑 `obexd -n -d` 得到 `Name already in use` 后退出，但没有继续查它在风暴里为什么失败，成因里"为什么失败"仍未确认。
更干净的做法是内层使用独立的 `XDG_CONFIG_HOME` / 用户管理器，但那等于改变结构，只提出来。

### R13 实测一段游戏串流的 GPU 与 CPU 数据（2026-11-01 23:38 到 23:45）

条件：RTX 5080，Proton 游戏 *Ready Or Not*（Unreal，Vulkan/DXVK/VKD3D，显存约 5.0 GiB），客户端 2560×1600、请求 120 fps，AV1 NVENC，
码率 34.99 Mbps，SDR 8-bit，外层缩放 1.5（逻辑尺寸 1706×1066），内层 KWin 固定 60 Hz。客户端在 23:38:09 连上、23:44:57 断开（约 6 分 48 秒）。
采样：`nvidia-smi dmon -s pucvmet -d 1`，日志在 `~/s1/forensics/dmon.log`，期间 `prism.service` 没有任何错误或丢帧类日志。

| 阶段 | 样本 | 功耗 W 均值/最大 | SM 利用率 % 均值/最大 | 显存带宽 % 均值 | NVENC % 均值/最大 | 显存 MB 均值/最大 |
|---|---|---|---|---|---|---|
| 串流 + 游戏（23:38:20 到 23:44:56） | 378 s | 131.6 / 235 | 47.1 / 96 | 16.9 | 11.6 / 13 | 6166 / 7000 |
| 同一局游戏、断开后（23:45:05 起） | 38 s | 172.3 / 207 | 58.7 / 80 | 24.0 | 0 | 6716 / 6797 |

- NVENC 约 12%（AV1，2560×1600 物理像素，35 Mbps）。没有功耗或温度限制标志（`pviol` 最大 2%、`tviol` 为 0），温度 54 到 59 °C，
  `nvidia-smi` 的应用列表里有 `prism`（270 MiB）、`prism-labwc`（21 MiB）、内层 `kwin_wayland`（27 MiB），外层与内层合成器的显存开销很小。
- CPU：游戏进程（`ps` 里显示为 `GameThread`）累计约 6 个核（618%）；`prism` 约 23% 累计均值（当前约 7%）、外层 `labwc` 约 22%（累计均值）、`plasmashell` 约 8%。
  整机 CPU 空闲约 55%，内存已用 14.8 GiB。
- **不能下结论的：** 断开后 GPU 反而更忙（SM 59% 对 47%、功耗 172 W 对 132 W）。这两段游戏场景不一定相同（我不知道断开后用户在做什么），
  样本只有 38 秒，所以不能据此说"串流让游戏更省"；一种可能是串流时游戏被内层 60 Hz 输出限帧，**这只是猜测**，要用游戏内帧率显示或
  `MANGOHUD` 之类在两种条件下对照才能确认。延迟（端到端）这次没有测，客户端统计叠加层的数字还没拿到。
- 日志里每秒会有几十行 `radv: RADV_PERFTEST=video_encode is deprecated`（25 分钟累计 768 行），来自 Prism 的 Vulkan 探测路径，和串流无关，只是噪声。

### R14 内层 KWin 因"与外层的 Wayland 连接断开"自杀，整个桌面和游戏一起消失（2026-11-02 00:01:36，原因未明）

现象：用户在关卡里测 Super / Alt+Tab（这两个键本身没有问题）时，Plasma 桌面和游戏同时消失。客户端 00:01:28 重新连上（外层输出先是 1280x720，随后
2560x1600，缩放 1.5，AV1，与之前十几次重连完全一样），8 秒后 00:01:36 内层 `kwin_wayland`（pid 15482）以 SIGABRT 退出，
`kwin_wayland_wrapper` 随后拉起一个新的 `kwin_wayland`（pid 670056），但 `plasmashell`、Steam 和游戏都不在了（Steam 报
`XIO: fatal IO error 110 on X server ":2"`，游戏的 `wine64-preloader` 在 00:01:58 段错误，应该是随后的连锁结果）。

能确认的：日志里 `kwin_wayland[15482]: Wayland connection broke`，栈顶是
`KWin::Wayland::WaylandEventThread::dispatch()` → `QMessageLogger::fatal` → `abort`，即**嵌套后端在外层连接出错时直接 `qFatal`，没有重连或降级路径**。
外层 `prism-labwc` 进程没有退出（`prism-headless-session` 一直 active），同一时刻的 GPU 数据正常（NVENC 在 00:01:29 起约 12%，00:01:37 后随客户端
断开降为 0），内核日志本次启动之后没有新条目。能确认的另一点是它和 R11 的冻死不是一回事：这次整机没有冻。

不能确认的：外层为什么断开这条连接。我没有拿到外层 `labwc` 在 00:01:36 的 stderr（`~/.local/state/prism-headless.log` 的最后修改时间是 23:44，
时间戳又是相对于各自进程启动的，对不上）；`coredumpctl` 里这个核心的状态是 `inaccessible`（普通用户读不了，需要 `sudo coredumpctl info 15482`）。
可能是 labwc 向这个客户端发了协议错误，也可能是别的，**我没有证据区分**。也不知道它是否与 Super/Alt+Tab、重连时的分辨率变化或缩放有关
（同样的操作之前没有触发过，也无法排除巧合）。

含义：这是嵌套结构一个新的、有分量的脆弱点——内层 KWin 是外层的客户端，**外层连接上任何一次错误都会杀掉整个内层桌面和里面所有进程**，用户会话和游戏状态全丢。
直连 KWin（S2）没有这一层。但目前只发生了一次，我没有复现，所以只能作为"需要关注的风险"，不是结论。
下一步（需要你同意）：用 `sudo coredumpctl info 15482` 看有没有更多线索；重建会话后重复同样的操作看能否复现；复现时同时抓外层 `labwc` 的 stderr。
重建时丢失的状态：内层会话和游戏（Steam 登录和着色器缓存还在磁盘上，但每次重建都会重新处理，见前面的 Steam 着色器记录）。

### R15 虚拟手柄（2026-11-02 00:08 到 00:20）：权限补上之后全部可用

现象：客户端连了手柄，Prism 日志有 `Gamepad 0 will be a Xbox One controller (automatic selection)`，内核日志有
`input: Prism X-Box One (virtual) pad`（00:08:51、00:10:02），说明虚拟手柄创建成功，但内层 Plasma 和 Steam 里看不到。

原因（用同名 `uinput` 假设备复现）：手柄的 `event22` 和 `js1` 是 `root:input`，ACL 里只有 `plasmalogin`（本机登录界面），`jhuang` 不在 `input` 组，
无座位会话没有 `uaccess`，所以用户进程打不开。这是 F9 / R3 / R9 的**第四类设备**（`card*`、`uinput`/`uhid`、`passthrough` 键鼠之外，还有
按需创建的手柄节点），M3 的授权方式必须覆盖它，而且它的名字是 Prism 生成的（`Prism ... (virtual) pad`），规则不能只写固定的键鼠名字。
临时处理：给 `/run/udev/rules.d/62-prism-s1.rules` 追加 `SUBSYSTEM=="input", ATTRS{name}=="Prism*virtual*", OWNER="jhuang", MODE="0660"` 并 `udevadm control --reload`，
再用假手柄确认节点属主变成 `jhuang`（00:13）。

结果（用户在 Steam 里测）：**所有按键被正确识别，震动可用**（Xbox One 类型，经 `uinput`；DualSense 走 `uhid` 的路径没有测）。
未测：多手柄、陀螺仪/触摸板（DualSense 才有）、手柄在 Plasma 本体里的识别（只测了 Steam 内）。

### 用户对剩余 SDR 项的取舍（2026-11-02 00:30）

- 缩放下的鼠标坐标：用户在 1.25 下测过，没发现问题；1.5 在 R13 的整段游戏里正常使用；1.75 没测。
- 原生 Wayland 客户端的指针锁定：用户没有原生 Wayland 游戏可测；R10 的锁定问题至今只在那一款游戏（Xwayland/Proton）里出现，其他应用没有。**这一项没有验证**，
  不能据此说明问题只出在 Xwayland 路径。
- R14（内层 KWin 崩溃）的复现：用户判断随机性太强，暂时跳过。**没有复现，原因仍未明。**
- 下一步：HDR，需要换用支持 HDR 的手机 Moonlight 客户端。

### R16 HDR：嵌套内层 Plasma 不出 HDR，外层直连 HDR 目测正常（2026-11-02 00:21 到 00:29）

条件：客户端是支持 HDR 的手机 Moonlight（3056x1440@90），设置里打开 HDR，首选 HEVC；`prism-labwc` 外层输出 `HEADLESS-1`，
外层提供 `wp_color_manager_v1`（支持 `st2084_pq`、`gamma22`、`bt1886`、`ext_linear`，色彩表示协议支持 BT.2020）。

**A. 嵌套的 S1 Plasma（00:21 到 00:24，AV1 和 HEVC 各连一次）：**
- Prism 一侧：`Headless HDR calibration: SDR white 203 nits, display peak 1000 nits`，`Color coding: HDR (Rec. 2020 + SMPTE 2084 PQ)`，`10-bit`，
  所以串流容器是 HDR，没有 `Headless HDR requested but the output is not BT.2020/PQ` 报错。
- 内层 KWin：`kscreen-doctor -o` 对输出 `WL-0` 报 `HDR: incapable`、`Wide Color Gamut: incapable`、`Allow EDR: unsupported`、`ICC profile: incapable`。
- 用户看到：画面是 SDR；Chrome 里 YouTube 没有 HDR 选项。
- 结论（真机确认 VM 里的 F3）：**内层 KWin 在嵌套后端下不提供 HDR 输出**，桌面里的应用没有机会输出 HDR。外层确实暴露了颜色管理协议，
  但内层没有用它（内层输出的能力读数是 incapable）。我没有读 KWin 的源码确认"嵌套后端没有实现颜色管理"这句话，只确认了读数和现象。

**B. 对照实验 `S1 HDR mpv`（00:28 到 00:29）：** 在外层 `prism-labwc` 里直接全屏播放 HDR10 PQ 渐变片（`~/s1/hdr/pq-gradient.mkv`，BT.2020 + ST 2084，10-bit，
`ffprobe` 确认帧里有这三个标记），不经过内层 Plasma；mpv 用 `--vo=gpu-next --gpu-context=waylandvk --target-colorspace-hint=yes`（Flatpak 版 0.41.0）。
- 用户目测"没问题"，即 HDR 渐变能正常显示。mpv 日志只有 `Video --vid=1 (hevc 1920x1080 30 fps)` 和 `VO: [gpu-next] 1920x1080 yuv420p10`，
  没有颜色空间协商的详细信息，所以**这一条的证据只有用户肉眼，没有测量**：没有测峰值亮度、没有核对 PQ 曲线是否准确、也没有量化 SDR 与 HDR 的差别。
- 能说明的：外层合成器的 HDR 路径、`wlgrab` 采集、10-bit HEVC NVENC、网络、手机解码这一整条链路在这台机器上是通的。

含义：**HDR 的障碍就在"内层 KWin"这一层，不在 Prism 的 HDR 链路。** 对 ADR-0005 来说，偏向嵌套的核心理由是"采集、输入、HDR 只实现一次"，
这一条里的"HDR"在嵌套下目前拿不到：桌面和里面的应用（Steam、游戏、浏览器）都只能是 SDR，HDR 只对直接跑在外层的客户端有效。
未验证：新版 KWin 嵌套后端是否有计划支持；让内层 KWin 以 `--virtual` 或 DRM 后端运行（S2）时 HDR 是否可用（这才是需要比较的）；
游戏在直连外层时（不经内层）的 HDR；`prism-hdr-calibration`（Prism 自带的 "Headless HDR Configuration" 应用）没有测。

**后续方向记录（2026-11-02，暂缓，未验证）：** HDR 缺失的层级是 KWin 后端，不是硬件或驱动：嵌套输出 `WL-0` 和 `--virtual` 输出 `Virtual-0`
都不经过 GPU 显示驱动，却同样报 incapable；外层 labwc 的 HDR 链路已由对照实验确认可用。我读了 KWin 上游 **master**（不是机器上的 6.7.5）的源码：
- 虚拟后端创建输出时只设置 `CustomModes` 能力，没有 HDR 和 WCG。
- 嵌套（Wayland）后端创建输出时只设置 `Dpms`，以及外层支持时的 `Tearing`，没有 `HighDynamicRange`。
- 嵌套后端已有一部分色彩管理接线：`wayland_output.cpp` 向外层请求 `wp_color_manager_v1` 的 surface 反馈，`wayland_layer.cpp` 在颜色变化时调用
  `set_image_description`；但特性检查里只看到 `PARAMETRIC`、`SET_PRIMARIES`、`SET_LUMINANCES` 和 `GAMMA22`，没有看到 PQ。

由此得到一个**未验证的假设**：给嵌套后端打补丁，在外层支持 PQ 和 BT.2020 时打开 HDR/WCG 能力并让 PQ 走通，可能让内层 Plasma 出 HDR。
没有检查的部分：6.7.5 与 master 的差异、10-bit/fp16 缓冲格式的路径、HDR 输出配置与亮度设置的路径。
`--virtual` 后端没有外层 surface，HDR 要经 ScreenCast 流带出去，可行性更低。
另有两条未验证的路线：Prism 的 `virtual` 采集模式配合本地已登录的真实 DRM 桌面（日志里出现过 `enabling hdr/wcg on Virtual-Prism-Virtual`，画面是否真 HDR 没测）；
以及在外层 labwc 里用 gamescope 只给游戏做 HDR。
用户决定：先把 roadmap 主干功能做完，HDR 和性能优化之后再处理，这里只记方案。

### 真机环境搭建要点（Kinoite 不可变系统）

不需要 `rpm-ostree install`，所有编译都在 podman 里完成，产物装到 `~/.local`：
- **`prism-labwc`**：Fedora 44 镜像（`dnf install` 编译依赖：`meson ninja-build glslang wayland-protocols-devel
  vulkan-headers vulkan-loader-devel xorg-x11-server-Xwayland-devel lcms2-devel libinput-devel xcb-util-wm-devel` 等），
  `podman run -v $PWD:/src -v <out>:/out -e PRISM_INSTALL_PREFIX=/out ... bash contrib/virtual-session/build-headless-compositor.sh`，
  再把 `<out>/bin/prism-labwc` 拷到 `~/.local/bin`。宿主同为 Fedora 44，`ldd` 无缺失库。
  这只是 Fedora 版的验证用法；AppImage 里自带的 `prism-labwc` 由官方 Ubuntu 构建流程产出（见 R6）。
- **Prism 本体的测试**：`docs/roadmap/03-conventions.md` 的 podman 配方。用 git worktree 时有三个坑，否则 cmake 会
  静默下载错误的 "latest" FFmpeg，链接报 `swr_*` 缺失：
  1. `scripts/checkout-linux-submodules.sh` 浅克隆 build-deps，没有 tag，要手动
     `git -C third-party/build-deps fetch --depth=1 origin tag v2026.910.121303`；
  2. 子模块的 `.git` 文件用相对路径指向 `/prism/.git/...`，`core.worktree` 指向 `/prism-<worktree 名>`，所以要把主仓库的 `.git`
     只读挂到 `/prism/.git`，并把 worktree 再挂一份到 `/prism-<worktree 名>`；
  3. 日志里要看到 `Using FFmpeg from build-deps tag: ...`，而不是 "latest"。
  4. 官方 AppImage 构建（Ubuntu builder）的打包阶段还要在 `/src` 里跑 git，所以再把主仓库 `.git` 只读挂到它的宿主原路径
     （`-v <主仓库>/.git:<主仓库>/.git:ro`），否则最后一步报 `not a git repository`。
- **会话隔离**：S1 的前提是本地没有 Plasma。注销本地会话后用 SSH，并开启 linger，否则用户管理器会随会话结束。

### 真机测试剩余项（下一步）

按优先级：
1. ~~装好带修复的 AppImage，确认 NVENC 通过探测~~：已完成（R6，SDR 探测通过）。还剩 HDR 编码路径要在真实串流里确认。
2. ~~解决 `/dev/uinput` 权限~~：已完成（R7，还有 `/dev/uhid`）。接下来用 PC Moonlight、Android Moonlight 和 Iris 配对串流。
3. S1 的真机步骤（见上节"真机验证步骤"）：内层 Plasma 在 `prism-labwc` 里的 HDR（Q2）、FPS 游戏的相对指针与指针锁定、
   Super/Alt+Tab 是否被外层截获（Q3）、光标（Q4）、客户端改分辨率后内层是否跟随、延迟与 GPU 占用的绝对值
   （T0.2 已搁置，不做基线对比）。
4. S2 的真机部分：`kwin_wayland --virtual` 加 `kwingrab` 串流，对比 S1；EIS 输入的延迟和完整性；HDR 是否可用
   （VM 里已确认 6.7.5 的虚拟后端 incapable）。R2 到 R5 的发现同样适用于 S2。
5. S5 的客户端部分：Moonlight 和 Iris 的退出、恢复语义。
6. S6：S1 或 S2 通过后，`steam -gamepadui` 在会话里的手柄、HDR、帧率。

### 已经做的环境改动（均为临时，测试后需还原）

- `loginctl enable-linger jhuang`（还原：`loginctl disable-linger jhuang`）
- `setfacl -m u:jhuang:rw /dev/dri/card1 /dev/uinput /dev/uhid`（重启或 udev 重新触发后自动失效）
- `/run/udev/rules.d/62-prism-s1.rules`（passthrough 输入节点、`uinput`、`uhid`、`card1` 的属主设为 jhuang；还原：`sudo rm` 后 `sudo udevadm control --reload`，重启也会清掉）
- `~/.local/bin/prism-labwc`、podman 镜像 `prism-labwc-build`
- 手动起的 `smoke-labwc.service`，用户管理器环境里的 `WAYLAND_DISPLAY=wayland-0` 和 `XDG_SESSION_TYPE=wayland`
  （还原：`systemctl --user stop smoke-labwc`、`systemctl --user unset-environment WAYLAND_DISPLAY XDG_SESSION_TYPE`）
- 已安装的 Prism 被替换为带 NVENC 修复的构建（R6），旧版在 `~/.local/opt/prism.bak-20261101-184837`；
  `~/.local/opt/` 里还有上次的 `prism.bak-20261027-040315`
- git worktree：`../prism-nvenc-fix`（分支 `fix/nvenc-preset-numbering`，已推送到 `fork`）、`../prism-s1-build`
  （分支 `test/s1-nvenc-fix`，不推送）；podman 镜像 `prism-appimage-build`、`prism-labwc-build`
- S1 测试应用：`~/.config/prism/apps.json` 加了 "S1 Plasma"（备份 `apps.json.bak-s1-20261101`，还原：覆盖回去），脚本
  `~/s1/start-plasma.sh` 和垫片 `~/s1/shim/kwin_wayland`；脚本运行时会把用户管理器的 `PATH` 改成带垫片的值
  （还原：`systemctl --user set-environment PATH=/usr/local/sbin:/usr/local/bin:/usr/bin`）。内层 Plasma 退出会拆掉整个
  用户管理器（F4），包括 `prism.service` 和 `smoke-labwc`，之后需要重新登录或启动用户管理器并重起这两个单元。
- 注意：`prism --version` 会加载并写配置（AppRun 里也有这条注释），在已有配置的机器上无害，但不要在别人的配置上随手运行。

## 给 ADR-0005 的暂时建议

数据不足，不能定论。目前没有发现嵌套结构的功能性阻碍（F1 有解，F2 协议基本齐全，F7 的显示、分辨率跟随、
对话框都通过），但键盘穿透、HDR、游戏输入
和性能是 ADR-0005 的核心依据，仍需真机数据。S2（直连 KWin headless）也尚未开始，**ADR-0005 保持
"提议"状态**。

## 对问题清单的更新建议

- Q2、Q3、Q4 仍为"待验证"，已有的部分数据见上。
- 建议新增风险：**无座位会话在 SELinux Enforcing 下的进程域**（F6），归入 S3 的验证点。
- 建议新增风险（真机补测发现，需要你决定是否写入清单，我没有改动问题清单）：
  - **Prism 启动探测要求有显示器**（R2）：没有任何图形会话时 Prism 无法启动，影响 M2 的"无人登录启动"；建议归入 T2.3
    或 S3 的验证点。
  - **无头模式（wlgrab）下没有可用的 GPU 编码路径**（R4、R5）：NVENC 预设编号问题（issue #106，已有修复分支
    `fix/nvenc-preset-numbering`）和 Vulkan 编码器的设备选择与帧转换问题；影响所有真机延迟数据。
  - **无座位会话的设备权限**（R3，F9 的真机确认）：`uaccess` 不授权给无座位会话，`card1` 和 `uinput` 需要额外授权；
    归入 S3 的验证点和 M3 的 helper 设计。

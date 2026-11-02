# 04 任务拆解

按里程碑推进。每张卡片包含：依赖、规模（S ≈ 1–2 天，M ≈ 3–5 天，L ≈ 1–2 周）、状态、内容、
验收标准。完成一张卡片的流程见 [README](README.md#每个任务的工作流)，完成标准见
[项目规范](03-conventions.md#12-完成标准definition-of-done)。

## 依赖总览

```
M0 ─► M1 ─┬─ S1, S2 ─► ADR-0005 ─► T2.3
          ├─ S5 ─────► ADR-0007 ─► T2.4
          ├─ S6 ─────► ADR-0008 ─► T2.6
          ├─ S3 ─────► ADR-0002 细化 ─► M3
          └─ S4 ─────► M4
M2：T2.1 ─► T2.2 ─► T2.3 ─► T2.4 ─► T2.5 ─► T2.6
                     └────► T2.7          T2.8、T2.9 贯穿
M3：T3.1 ─► T3.2 ─► T3.3 ─► T3.4 ─► T3.5 ─► T3.6 ─► T3.7
M4：依赖 M3（T4.1 可在 M2 末期以用户级方式先做）
```

关键路径：**S1/S2 → T2.3 → T2.4 → T3.2 → T3.3**。

---

## M0 规划与基线

### T0.1 采纳规划文档
- 依赖：无　规模：S　状态：待验收
- 内容：本目录全部文档；`AGENTS.md` 增加指向本目录的说明。
- 验收：用户审阅通过；文档无尾随空格，`scripts/lint.sh` 通过。

### T0.2 性能与行为基线
- 依赖：T0.1　规模：S　状态：搁置（2026-10-31 用户决定跳过性能测试；M1 不再与基线对比，延迟/占用只记录绝对值）
- 内容：在当前版本上测量并记录（写入 `spikes/T0.2-baseline.md`）：
  - Steam Headless 和 Desktop Headless 的启动耗时（点击到首帧）；
  - 串流延迟：Moonlight/Iris 统计里的主机处理延迟、网络延迟、解码延迟（1080p60、4K60、4K120 HDR 各一组）；
  - 宿主 CPU/GPU 占用（`nvidia-smi dmon`、`top`）；
  - 断开、重连、退出的现有行为。
- 验收：有可复现的测量步骤和数据，后续实验用同一方法对比。

### T0.3 VM 端到端测试环境
- 依赖：T0.1　规模：M　状态：待验收
- 内容：podman 容器里没有 logind、PAM 会话和真实用户管理器，系统级功能需要虚拟机。搭建脚本化的
  Fedora KDE 虚拟机（libvirt/QEMU，virtio-gpu 或软件渲染），能够：安装 Prism、无人登录启动、用
  `moonlight-qt` 的命令行或测试客户端发起配对和串流、收集日志。
- 验收：一条命令拉起虚拟机并跑完"开机 → 无本地登录 → 远程连接 → 看到画面"的冒烟流程（M3 后才能
  全部通过，M0 只要求环境就绪）。

---

## M1 技术验证（spike）

每个实验产出一份报告（`spikes/<编号>-*.md`）和对应 ADR 的结论建议。实验代码可以是临时脚本，不要求
达到产品质量，但不得合入 `master`。

### S1 嵌套 Plasma 运行在 prism-labwc 中
- 依赖：T0.2（已搁置）、T0.3　规模：M　状态：待验收（VM 与真机均已测：SDR 游戏、输入、手柄、HDR 结论见 `spikes/S1-nested-plasma.md`；HDR 与性能留待主干之后）
- 前提：先注销本地 KDE 会话（例如通过 SSH 操作，或开启 linger），或使用另一个测试用户，避免同一用户
  两个 Plasma 冲突。
- 步骤：
  1. 用现有 `prism-headless-start.sh` 起外层（Desktop Headless）。
  2. 在外层中以 `WAYLAND_DISPLAY=<外层 socket> dbus-run-session startplasma-wayland` 起内层 Plasma。
  3. 用 Prism 串流外层输出。
- 验证点：画面与分辨率跟随；Plasma 面板、启动器、Dolphin 正常；文件对话框出现在串流画面；
  键盘布局与快捷键（Super、Alt+Tab）是否被外层截获；相对指针和指针锁定（用一个 FPS 游戏）；
  光标显示；HDR（内层 KWin 嵌套后端是否能把 HDR 表面交给外层）；Xwayland 应用；延迟与 GPU 占用
  对比 T0.2 基线；客户端分辨率变化时内层是否跟随。
- 产出：ADR-0005 的数据。

### S2 直连 KWin headless
- 依赖：T0.2　规模：M　状态：待验收（VM 与真机采集部分完成；真机输入需新写 EIS 转发桥，已按用户决定不做，见 `spikes/S2-direct-kwin-headless.md`）
- 步骤：`kwin_wayland --virtual --width W --height H` 起 Plasma；采集用现有 `kwingrab.cpp`
  （zkde_screencast）；输入试验 EIS（KWin 的远程桌面接口或 RemoteDesktop portal）和
  `org_kde_kwin_fake_input` 两条路径。
- 验证点：与 S1 相同，另加：EIS 输入的延迟和完整性（相对指针、滚轮、键盘布局）；HDR 能否在虚拟
  后端启用；KWin 对 headless 模式的稳定性。
- 产出：ADR-0005 的数据。

### S3 由系统单元为用户创建无座位会话
- 依赖：T0.3　规模：M　状态：待验收（VM 部分完成，见 `spikes/S3-seatless-session.md`）
- 步骤：写一个临时的 root 程序或 PAM 测试工具（如 `pamtester` 的会话模式），在无人登录时为测试用户
  打开 `pam_systemd` 会话（`Type=wayland`、`Class=user`、无座位、`Remote=yes`），在其中启动外层和
  内层。
- 验证点：`user@UID` 正常启动；PipeWire/WirePlumber 在无座位会话中工作（含空 sink）；
  `/dev/dri/renderD*` 和 NVIDIA 设备（`/dev/nvidia*`）可访问，NVENC/CUDA 可用；uinput 设备权限；
  polkit 对挂载、关机、NetworkManager 的判定（会话非 local/active 时的行为）；KWallet 能否解锁；
  注销后会话和单元全部消失；本机有本地会话时同时开远程会话的现象（为 S4 做准备）。
- 产出：ADR-0002 细化（helper 设计、PAM 栈）、问题 Q6/Q8/Q15 的结论。

### S4 远程会话存在时本地登录
- 依赖：S3　规模：S　状态：待验收（VM 部分完成，见 `spikes/S4-local-login-with-remote.md`）
- 步骤：远程 Plasma 会话处于断开状态，在物理座位通过 SDDM（以及 Plasma Login Manager，如可用）以同一
  用户登录。
- 验证点：显示管理器是否允许登录；第二个 Plasma 的具体故障（systemd 用户单元冲突、配置文件互写、
  kded/plasmashell 单例）；logind 能否让 Prism 提前感知（会话新建信号的时机）。
- 产出：M4 的策略选项和可行性。

### S5 Moonlight 退出/恢复语义
- 依赖：T0.2　规模：S　状态：进行中（服务端部分完成，客户端实测待做，见 `spikes/S5-quit-resume-semantics.md`）
- 步骤：在 Moonlight（Android、PC）和 Iris 上测试：退出时服务器不结束应用会发生什么；
  launch 时服务器返回已有会话的行为；resume 流程；切换到另一应用时客户端的提示。
- 产出：ADR-0007（"退出 = 断开保留会话"是否可行，客户端是否需要配合修改；Iris 可改，原版 Moonlight
  不可改）。

### S6 Steam 大屏幕在桌面会话中运行
- 依赖：S1 或 S2　规模：S　状态：未开始
- 步骤：在远程桌面会话里以 `steam -gamepadui` 全屏运行，与现有 Steam Headless（labwc + Steam）对比。
- 验证点：手柄（含 DualSense/Edge）识别、Steam Input；HDR 游戏；帧率与延迟；从大屏幕退回桌面；
  宿主手柄隔离是否还需要 bubblewrap（无本地会话时宿主手柄的影响）。
- 产出：ADR-0008（Steam 大屏幕是会话里的应用，还是独立的会话类型）。

---

## M2 会话成为一等对象（用户级服务）

M2 不引入系统服务。远程会话只在"用户管理器在运行但没有本地图形会话"时新建（开启 linger 或
SSH 登录），目的是先把会话模型、策略、组成和 app-sync 跑通。

### T2.1 会话对象与状态机
- 依赖：M1 结论　规模：M　状态：未开始
- 内容：`src/session/session.{h,cpp}`：会话 ID、类型（本地接入/远程）、状态机（见架构 3.7）、事件处理、
  版本化状态文件的原子读写与严格解析、启动时从状态文件和 logind 重建。
- 验收：状态机所有状态 × 事件组合有单元测试；损坏、过期、版本不符的状态文件都被拒绝；覆盖率达标。

### T2.2 logind 快照与接入/新建策略
- 依赖：T2.1　规模：M　状态：未开始
- 内容：`src/platform/linux/session/logind.{h,cpp}`：通过 sd-bus 读取用户的会话列表（类型、类别、
  座位、活动、远程、状态）并订阅新建/移除信号，接口可注入假实现；`src/session/policy.{h,cpp}`：
  架构 3.3 的纯函数策略。
- 验收：策略表每一行都有测试；logind 读取在假总线上测试；无 logind 时明确失败。

### T2.3 远程会话组成（外层 + 内层）
- 依赖：T2.2、ADR-0005　规模：L　状态：未开始
- 内容：`contrib/remote-session/`：会话目标和外层、输入桥、内层桌面、代理的单元模板与启动脚本；
  沿用现有就绪检测（socket、输出模式、协议、输入）和回滚；内层桌面可选 `plasma`、`labwc-minimal`、
  `custom`；扩展 `prism-session-cleanup.sh` 协调新单元和状态文件。
- 验收：在 linger 环境下新建会话、串流、断开、重连、注销全流程可用；文件对话框出现在串流画面；
  脚本测试覆盖就绪超时、部分失败回滚、重复清理；注销后无残留（单元、scope、sink、状态文件）。

### T2.4 应用生命周期与会话生命周期解耦
- 依赖：T2.3、ADR-0007　规模：L　状态：未开始
- 内容：新增捕获模式 `session`；`proc_t` 中 `session` 模式的 launch 走策略（attach/spawn），
  quit 按 ADR-0007 处理为 detach 或 end；应用退出不结束会话；`prism_capture_begin/end` 的改动限定在
  接缝调用；现有模式行为不变。
- 验收：`test_process.cpp` 覆盖 `session` 模式的启动、恢复、退出、应用退出；现有模式的测试全部通过；
  手动验证 Moonlight 与 Iris 的完整流程。

### T2.5 在会话内启动应用并切到前台
- 依赖：T2.4　规模：M　状态：未开始
- 内容：`prism-session-exec`：在会话环境里以 scope 启动命令（所有权校验同 `prism_run_owned_app`）；
  启动后激活窗口（Plasma 用 KWin D-Bus，labwc 用 foreign-toplevel）；会话不存在时先新建。
- 验收：Firefox、Dolphin、一个 Flatpak 应用、一个 Xwayland 应用都能启动并在前台；重复点击同一单实例
  应用时激活已有窗口；脚本测试覆盖参数校验和失败路径。

### T2.6 app-sync 改走会话
- 依赖：T2.5、ADR-0008　规模：S　状态：未开始
- 内容：同步应用的默认捕获模式从 `headless` 改为 `session`；Steam 游戏按 ADR-0008 处理；保留用户在
  `apps.json` 中的覆盖；README 的 app-sync 小节更新。
- 验收：`test_app_sync.cpp`、`test_config_app_sync.cpp` 更新并通过；两个应用先后打开后能互相
  拖放文件、共享剪贴板。

### T2.7 接入本地会话
- 依赖：T2.2　规模：M　状态：未开始
- 内容：策略返回 attach(本地) 时复用 Desktop (Virtual) 的虚拟输出路径；新增物理屏幕策略配置项
  （保持点亮 / 关闭 / 关闭并在断开时锁屏），默认值与现有行为一致（关闭）。
- 验收：三种策略手动验证；配置项通过 `ConfigConsistencyTest`；断开后物理输出按记录恢复。

### T2.8 Web UI：会话状态与控制
- 依赖：T2.4　规模：M　状态：未开始
- 内容：新 API（`GET /api/sessions`、`POST /api/sessions/<id>/end`）和页面区块：会话类型、状态、
  持续时间、当前客户端、结束按钮；新配置项（内层桌面、退出语义、空闲超时、物理屏幕策略）。
- 验收：`test_confighttp.cpp` 覆盖新 API（含 CSRF 与认证）；文案只改 `en.json`；配置项四处一致。

### T2.9 M2 文档
- 依赖：T2.3–T2.8　规模：S　状态：未开始
- 内容：README（捕获模式表、How it works）、`docs/configuration.md`、本目录状态更新。
- 验收：文档与实现一致；新用户按 README 能在 linger 环境下用起来。

---

## M3 系统级服务

### T3.1 IPC 协议
- 依赖：S3、M2　规模：M　状态：未开始
- 内容：`src/session/ipc.{h,cpp}`：`SOCK_SEQPACKET` + `SO_PEERCRED`；带版本的 JSON 消息；消息类型、
  字段白名单、长度上限；prismd↔helper、prismd↔代理两套消息定义。
- 验收：编解码、版本不符、未知字段、超长消息、对端身份不符的测试；模糊测试至少覆盖解析器。

### T3.2 拆出 prismd 与会话代理
- 依赖：T3.1　规模：L　状态：未开始
- 内容：同一可执行文件支持 `--role=prismd` 和 `--role=agent`（或两个入口共享库）；prismd 只启用
  nvhttp、Web UI、配对、代理器；代理只启用 RTSP、串流、采集、编码、音频、输入；launch 参数经 IPC
  从 prismd 传给代理。
- 验收：两个角色在 VM 中协同完成一次完整串流；角色之间不共享可写状态文件；上游文件的改动集中在
  少数接缝。

### T3.3 prism-session-helper 与 PAM 会话
- 依赖：T3.2　规模：M　状态：未开始
- 内容：套接字激活的 root helper；`/etc/pam.d/prism-remote`；打开和关闭会话、在 `user@UID` 中启动
  会话目标；全部参数白名单校验。
- 验收：VM 中无人登录时可新建会话；helper 拒绝非 `prism` 用户的连接和未绑定用户；注销后 logind
  中无残留会话。

### T3.4 设备↔用户绑定与认证
- 依赖：T3.2　规模：M　状态：未开始
- 内容：绑定数据存于 `/var/lib/prism`；Web UI 绑定页面；可选 PIN/系统密码（新建会话时、接入时分别
  可配）；PIN 限速与锁定；按绑定用户返回应用列表。
- 验收：未绑定设备无法新建或接入会话；限速生效；测试覆盖。

### T3.5 配置与数据迁移
- 依赖：T3.4　规模：M　状态：未开始
- 内容：`prism --migrate-system`：把 `~/.config/prism` 中的主机级配置和凭据搬到 `/etc/prism`、
  `/var/lib/prism`，用户级内容留在原处；可重复执行；失败可回滚。
- 验收：迁移前后配对设备无需重新配对；HDR 校准、apps.json、app-sync 选择保留。

### T3.6 打包与安装
- 依赖：T3.3　规模：L　状态：未开始
- 内容：系统级安装（`sysusers.d`、`tmpfiles.d`、系统单元、PAM 文件、polkit 规则、udev 规则）；
  AppImage 的系统安装路径（如 `/opt/prism`）；`install.sh`、`prism --update`、`prism --remove` 支持
  系统模式；用户级模式继续可用。
- 验收：`test_install_policy.sh`、`test_appimage.py` 等更新并通过；VM 中安装、升级、卸载、迁移全流程。

### T3.7 安全加固与审查
- 依赖：T3.6　规模：M　状态：未开始
- 内容：单元加固选项逐项启用并验证；威胁模型复核（架构第 6 节）；`systemd-analyze security` 评分；
  日志脱敏检查。
- 验收：审查记录写入 `spikes/T3.7-security-review.md`；高风险项全部关闭。

### T3.8 无座位会话的 polkit 规则
- 依赖：S3　规模：S　状态：未开始
- 内容：按 S3 结论，提供只放开明确列出的操作的 polkit 规则（如挂载可移动存储、NetworkManager 用户
  连接），与 Prism 远程会话绑定（按用户组或会话属性）。
- 验收：远程会话里挂载 U 盘不再要求管理员密码；未列出的操作行为不变。

---

## M4 本地与远程共存

### T4.1 远程会话存在时的本地登录处理
- 依赖：S4、T2.2　规模：M　状态：未开始
- 内容：监听 logind 新会话；按配置执行：提示（在远程会话里通知）、结束远程会话、或保持并警告。
- 验收：三种策略在 VM 中验证；不会出现同一用户两个 Plasma 同时运行而无人知晓的情况。

### T4.2 "注销本地，远程重开"
- 依赖：T3.3　规模：M　状态：未开始
- 内容：客户端条目或 Web UI 操作；二次确认；通过 logind 结束本地会话后新建远程会话。
- 验收：本地会话被干净注销（应用收到正常的退出信号）；远程会话随后建立。

### T4.3 接入本地会话时的锁屏与隐私
- 依赖：T2.7　规模：S　状态：未开始
- 内容：断开时按配置锁屏；本地屏幕关闭期间本地键鼠的处理。
- 验收：手动验证。

---

## M5 体验打磨

| 编号 | 内容 | 规模 |
|---|---|---|
| T5.1 | HiDPI：按客户端屏幕尺寸和 DPI 设置输出缩放（现在固定 `--scale 1`） | M |
| T5.2 | 中文输入法：远程会话里 fcitx5 可用（Plasma 内的 input-method 协议） | M |
| T5.3 | 剪贴板与文件传输（需 Iris 配合，评估 Moonlight 协议扩展） | L |
| T5.4 | 麦克风回传 | M |
| T5.5 | 会话空闲超时、断开后资源降级（降低刷新率等） | S |
| T5.6 | 远程会话里的手柄当鼠标 | S |

## M6 多用户并行（远期）

| 编号 | 内容 |
|---|---|
| T6.1 | 多个活动串流：端口复用或每会话端口；编码资源调度 |
| T6.2 | 按用户的应用列表、配额（GPU、内存） |
| T6.3 | 设备隔离：每个会话的手柄只对该会话可见 |
| T6.4 | 不同用户的会话并发时的音频、输入隔离验证 |

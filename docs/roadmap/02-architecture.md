# 02 架构规划

本文描述现状、目标架构、组件职责和迁移路径。标注"待定"的部分取决于 M1 的实验结果，决定后
记录在 [decisions.md](decisions.md)。

## 1. 现状（as-is）

### 1.1 进程与服务

```
物理座位 ── SDDM ── 本地 KDE 会话（KWin, DRM）
                         │ graphical-session.target
                         ▼
               prism.service（systemd 用户服务, PartOf=graphical-session.target）
               └─ prism 进程：nvhttp（配对/应用列表/启动）+ RTSP + 串流 + 采集 + 编码 + 输入 + Web UI
                    │ proc_t::prism_capture_begin()/end()（src/process.cpp）
                    ├─ default/portal → 镜像本地屏幕，prism-mirror-audio.sh
                    ├─ virtual        → prism-virtual-start.sh：krfb-virtualmonitor 在本地 KWin 加虚拟输出
                    └─ headless       → prism-headless-start.sh：
                         prism-headless-session.service（labwc/prism-labwc, WLR_BACKENDS=headless）
                         prism-input-bridge.service（虚拟指针/键盘）
                         prism-headless-app-<id>.scope（应用命令）
                         prism-headless-steam.service（可选）
```

### 1.2 关键事实

| 事实 | 位置 |
|---|---|
| 服务只在本地图形会话里运行 | `contrib/virtual-session/prism.service` |
| Moonlight 同一时刻只能有一个"运行中的应用"；已有应用时再启动会返回 "An app is already running on this host" | `src/nvhttp.cpp` launch 处理 |
| 捕获模式随应用启动建立、随应用退出拆除；headless 会话的生命周期等于应用的生命周期 | `src/process.cpp` `prism_capture_begin/end`、`proc_t::terminate` |
| headless 单元名是固定的单例（`prism-headless-session.service` 等），一次只能有一个 headless 会话 | `prism-headless-start.sh`、状态文件 v4 校验 |
| headless 会话的隔离 `XDG_CONFIG_HOME` 只有 `rc.xml`，没有任何桌面外壳 | `prism-headless-session.sh` |
| headless 会话和应用继承用户管理器的 `DBUS_SESSION_BUS_ADDRESS`、`XDG_CURRENT_DESKTOP=KDE` | `systemctl --user show-environment`，脚本未覆盖 |
| 采集：headless 用 wlroots screencopy（`wlgrab.cpp`），虚拟屏幕用 KWin zkde_screencast（`kwingrab.cpp`），另有 portal、KMS、X11 | `src/platform/linux/` |
| 键鼠：输入桥抓取 Prism 自己的 passthrough evdev 设备，转成 labwc 的虚拟指针/键盘；手柄是全局 uinput 设备，Steam 用 bubblewrap 隐藏宿主手柄 | `prism-input-bridge.c`、`prism-headless-steam.sh` |
| 音频：`prism-stream` 空 sink 作为采集源，每种模式把对应音频环回进去 | `prism-*-audio.sh`、`prism-audio-common.sh` |
| 崩溃恢复：启动前和 `ExecStopPost` 运行幂等的清理脚本 | `prism-session-cleanup.sh` |

### 1.3 问题与根因

| 编号 | 现象 | 根因 |
|---|---|---|
| P1 | 本地不登录就无法远程 | 服务挂在 `graphical-session.target` 上；没有登录前的进程，也没有创建会话的能力 |
| P2 | headless 桌面是空的 | 隔离配置目录里没有桌面外壳；labwc 只是合成器 |
| P3 | headless 应用的文件对话框弹到物理屏幕 | headless 会话不是独立会话，共用本地会话的 D-Bus 和 portal |
| P4 | app-sync 的应用之间不能协作 | 每个应用一个 headless 会话，会话随应用销毁 |
| P5 | 断开即销毁（对 headless 应用退出而言） | 会话生命周期绑定在 Moonlight 应用上 |

P3 不是"少了某个功能"，而是 headless 会话被定位成本地会话的"侧窗口"。目标架构从定位上解决它。

## 2. 目标架构总览

```
                 客户端（Moonlight / Iris）
        HTTPS 47984 / HTTP 47989           RTSP 48010 / ENet 47999 / UDP 47998, 48000
        （配对、应用列表、启动、恢复）       （串流，仅在有活动串流时由会话代理绑定）
                    │                                   │
                    ▼                                   ▼
  ┌─────────────────────────────────┐    ┌──────────────────────────────────────┐
  │ prismd（系统服务, 用户 prism）    │    │ 会话代理（以 alice 身份, 在会话内）     │
  │ - nvhttp：配对、应用列表、启动     │IPC │ - RTSP、串流、编码、音频、输入          │
  │ - Web UI、管理员认证              │◄──►│ - 采集（wlgrab / kwingrab）            │
  │ - 设备↔用户绑定、PIN              │    │ - 在会话内启动/聚焦应用                 │
  │ - 会话代理器：策略、logind 观察    │    └──────────────────────────────────────┘
  └──────────────┬──────────────────┘                 ▲ 运行于
                 │ IPC（仅限少数特权操作）                │
                 ▼                                      │
  ┌─────────────────────────────────┐                   │
  │ prism-session-helper（root, 最小）│                   │
  │ - 为用户打开/关闭 PAM 会话         │                   │
  │ - 在 user@UID 中启动会话单元       │                   │
  └──────────────┬──────────────────┘                   │
                 ▼                                      │
  logind ── user@1000.service ───────────────────────────┤
              ├─ 远程会话（Class=user, Type=wayland, 无座位, Remote=yes）
              │    外层：prism-labwc（headless 输出, screencopy, 虚拟输入）
              │    内层：嵌套的 Plasma（startplasma-wayland, WAYLAND_DISPLAY=外层）  ← ADR-0005 已选嵌套
              │    旁路：Steam 大屏幕直接作为外层客户端（ADR-0008，待 S6）
              └─ 或：本地会话（座位 seat0, KWin DRM）+ 虚拟输出（接入模式）
```

核心思想：

1. **网络面和会话面分离。** 登录前就要在线的部分（配对、应用列表、启动请求、Web UI）放在系统
   服务里；和用户桌面打交道的部分（采集、输入、音频、启动应用）以用户身份运行在会话内部。
   Windows 版 Sunshine（系统服务把串流进程拉进用户会话）和 GNOME 远程登录（系统守护进程加会话内
   handover 守护进程）都是这个结构。
2. **远程会话是真正的 logind 会话。** 由 PAM 创建，和显示管理器创建的会话地位相同；桌面环境像
   平时一样把环境导入用户管理器，portal、通知、`xdg-open` 自然落在这个会话里（解决 P3）。
3. **采集和桌面分离（嵌套）。** 外层合成器只做"串流相关"的事：按客户端模式生成输出、零拷贝采集、
   注入输入、HDR；内层是任意完整桌面。Wolf 用 gst-wayland-display 作为外层、sway/gamescope/XFCE
   作为内层，是同一思路。Prism 已有外层（`prism-labwc`）。
4. **一个用户一个图形会话。** 有本地会话就接入它，没有就新建；从结构上避免同一用户两个桌面冲突。

## 3. 组件

### 3.1 prismd（系统服务）

| 项 | 说明 |
|---|---|
| 运行身份 | 专用系统用户 `prism`（`sysusers.d` 创建），不以 root 运行。 |
| 启动 | `prismd.service`，`WantedBy=multi-user.target`，开机即运行。 |
| 职责 | nvhttp（serverinfo、配对、应用列表、launch、resume、cancel）、Web UI、管理员认证、设备↔用户绑定、PIN 校验、会话代理器（策略、logind 观察）、会话代理的生命周期管理。 |
| 不做 | 采集、编码、音频、输入，以及任何需要进入用户桌面的事。 |
| 数据 | `/etc/prism/`（主机级配置）、`/var/lib/prism/`（凭据、配对证书、设备绑定），权限 `prism:prism 0700`。 |
| 从现有代码来 | `nvhttp.cpp`、`confighttp.cpp`、`crypto.cpp`、`config.cpp` 的主机级部分。拆分方式是"同一套代码、按启动模式启用不同子系统"，避免复制代码。 |

### 3.2 prism-session-helper（root 辅助进程）

| 项 | 说明 |
|---|---|
| 为什么需要 | 打开 PAM 会话（`pam_systemd` 向 logind 注册会话）需要 root；网络面进程不应持有 root。 |
| 形式 | systemd 套接字激活的小程序，只接受来自 `prism` 用户的连接（`SO_PEERCRED` 校验）。 |
| 接口 | 只有少数几个操作：`open_session(user)`、`close_session(session)`、`start_unit(user, unit)`。参数逐项校验，不接受命令行字符串。 |
| PAM | 自带 `/etc/pam.d/prism-remote`：`account`/`session` 栈，`auth` 由 prismd 的绑定/PIN 完成（可选再走 PAM `auth` 校验系统密码）。会话参数：`XDG_SESSION_TYPE=wayland`、`XDG_SESSION_CLASS=user`、无 `XDG_SEAT`、`Remote=yes`。 |

### 3.3 会话代理器与策略

策略是一个**纯函数**，便于穷举测试：

```
输入：设备绑定的用户；该用户在 logind 中的会话快照（类型、类别、座位、是否活动、是否远程、状态）；
      Prism 管理的远程会话记录；客户端请求（条目类型、是否要求重开）；配置
输出：attach(本地会话) | attach(远程会话) | spawn | end_local_then_spawn | reject(原因)
```

| 用户状态 | 请求 Desktop | 请求"注销本地并重开" |
|---|---|---|
| 无会话 | spawn | spawn |
| 仅有 Prism 远程会话（已断开） | attach(远程) | attach(远程) |
| 仅有 Prism 远程会话（正被另一客户端串流） | 按配置：抢占（踢掉旧串流）或 reject | 同左 |
| 有本地图形会话 | attach(本地) | end_local_then_spawn（需二次确认） |
| 本地和远程会话同时存在（异常） | reject，并在 Web UI 提示 | reject |
| 用户未绑定 | reject（未授权） | reject |

### 3.4 会话代理

| 项 | 说明 |
|---|---|
| 是什么 | 现在的 prism 进程去掉 nvhttp 监听后的部分：RTSP、串流、采集、编码、音频、输入、应用启动。 |
| 运行身份与位置 | 以会话所属用户身份运行在 `user@UID` 中，环境来自该会话（`WAYLAND_DISPLAY`、`DBUS_SESSION_BUS_ADDRESS` 等）。单元：`prism-agent@<session>.service`。 |
| 启动 | prismd 决定 attach/spawn 后，经 helper 或用户管理器启动；接入本地会话时由 `graphical-session.target` 拉起（即现在的 `prism.service` 转为代理角色）。 |
| 与 prismd 通信 | Unix 套接字 IPC。prismd → 代理：launch 参数（`rikey`、`rikeyid`、客户端证书指纹、显示模式、HDR、音频选项、要启动的应用）。代理 → prismd：就绪、串流开始/结束、会话结束、错误。 |
| 端口 | 代理只在有活动串流时绑定 RTSP 和串流端口。M6 之前全局只允许一个活动串流，端口不冲突。 |

### 3.5 远程会话的组成

```
logind 会话（PAM 创建）
└─ user@UID
   ├─ prism-remote@<id>.target          会话的总目标，注销时整体停止
   ├─ prism-outer@<id>.service          外层：prism-labwc，WLR_BACKENDS=headless，HEADLESS-1
   ├─ prism-input-bridge@<id>.service   虚拟指针/键盘（沿用现有输入桥）
   ├─ prism-desktop@<id>.service        内层桌面：WAYLAND_DISPLAY=<外层 socket> startplasma-wayland
   ├─ prism-steam@<id>.service          旁路：steam -gamepadui，直接连外层（按需启动，待 S6 定形）
   └─ prism-agent@<id>.service          会话代理
```

- **内层桌面可选**：`plasma`（默认，已安装时）、`labwc-minimal`（兜底）、`custom`（用户命令）。
- **环境**：内层桌面自己执行 `dbus-update-activation-environment --systemd`（Plasma 会这样做），
  portal 后端按 `XDG_CURRENT_DESKTOP=KDE` 选择，对话框显示在内层桌面里。因为这是用户唯一的会话，
  **不使用私有 D-Bus**。私有总线只保留给过渡期的隔离 headless 应用模式。
- **游戏旁路（ADR-0008，待 S6）**：Steam 大屏幕和游戏不放进内层桌面，而是直接作为外层的客户端运行，与内层窗口
  并列，属于同一个 logind 会话和同一条会话总线。外层提供 HDR、指针约束和客户端刷新率，绕开 KWin 嵌套后端的限制。
- **已选嵌套（ADR-0005，2026-11-02）**：另一种结构是不要外层，直接 `kwin_wayland --virtual` 作为 headless 合成器，
  采集用已有的 `kwingrab.cpp`，输入走 EIS 或 KWin fake input。下表是规划时的取舍，实测结果见 ADR-0005：

| 维度 | 嵌套（外层 prism-labwc + 内层桌面） | 直连 KWin headless |
|---|---|---|
| 桌面环境无关性 | 好：采集/输入/HDR 只对外层实现一次 | 差：每种桌面各写一套采集和输入 |
| 合成次数 | 约三次：内层合成、外层合成、screencopy 拷贝（真机外层显存约 21 MiB） | 两次：虚拟输出自身合成，加 ScreenCast 重新渲染场景（KWin 6.7.5 源码，见 S2 报告） |
| HDR | 内层嵌套后端不报 HDR 能力（S1 R16）；直接跑在外层的客户端有 HDR | 虚拟后端不报 HDR，ScreenCast 只有 8 位 sRGB（S2） |
| 游戏输入（相对指针、指针锁定、快捷键抑制） | 两层都要支持并透传 | 只需 KWin 支持 |
| 现有代码复用 | `wlgrab.cpp`、输入桥、HDR 补丁 | `kwingrab.cpp`；输入需新写 |
| 分辨率变化 | 外层改模式，内层窗口随之全屏 | 直接改虚拟输出 |

### 3.6 接入本地会话

- 会话代理在本地会话中运行（即现在的用户级服务）。
- 用现有 Desktop (Virtual) 能力创建虚拟输出：`krfb-virtualmonitor`（`prism-virtual-start.sh`），
  备选 `prism-virtual-output.c`（zkde_screencast `stream_virtual_output`）。
- 物理屏幕策略按配置执行（保持、关闭、关闭并在断开时锁屏）。
- 采集：`kwingrab.cpp`；输入：现有路径（uinput 设备进入本地 KWin 的 libinput）。
- 因为就是同一个会话，不存在隔离问题。

### 3.7 会话生命周期状态机

```
            spawn 请求
  (none) ───────────────► spawning ──失败──► failed ──清理──► (none)
                              │ 就绪
                              ▼
           ┌──────────► streaming ──客户端退出/掉线──► detached
           │                │                            │
           │ attach         │ 用户注销/管理员结束          │ attach 请求
           │                ▼                            │
           └──────────── detached ◄──────────────────────┘
                            │ 空闲超时 / 用户注销 / 管理员结束 / 本地登录冲突策略
                            ▼
                         ending ──单元全部停止、PAM 会话关闭──► (none)
```

| 事件 | 来源 | 处理 |
|---|---|---|
| launch/resume | 客户端 → prismd | 走策略；attach 或 spawn |
| quit | 客户端 → prismd | 默认 detach，可配置为 end（ADR-0007） |
| 串流断开 | 代理 → prismd | detach |
| 会话内注销 | logind 会话移除 / 内层桌面退出 | ending；若在串流则结束串流 |
| 代理崩溃 | systemd | 会话保留为 detached；下次 attach 重启代理 |
| 外层或内层崩溃 | systemd | ending（会话无法恢复），通知客户端 |
| 本地同一用户登录 | logind 新会话信号 | 按 M4 策略处理 |
| prismd 重启 | — | 从 logind 和状态文件重建会话表，不影响运行中的会话 |

会话状态持久化在 `/run/prism/sessions/<id>.state`（版本化、原子写入，沿用现有 v4 状态文件的严格
解析风格）。

### 3.8 应用在会话内启动

- 代理在会话环境里运行 `systemd-run --user --scope --unit=prism-app-<id>-<n>.scope ...`，沿用
  `prism_run_owned_app` 的所有权校验思路。
- 启动后把窗口切到前台：嵌套 Plasma 时通过 KWin D-Bus（脚本接口）激活；labwc 时通过
  `wlr-foreign-toplevel-management`。
- 单实例应用（Firefox 等）由桌面自己的激活机制处理，因为它们和桌面在同一会话总线上。
- Moonlight 只允许一个"运行中的应用"。在会话模型里，"运行中的应用"对应"当前串流附着的入口"，
  它退出不影响会话；切换到另一个应用时客户端会要求先退出，退出即 detach，再 launch 新入口即可。

### 3.9 各模式的采集、输入、音频

| 模式 | 采集 | 键鼠 | 手柄 | 音频 |
|---|---|---|---|---|
| 远程会话（嵌套） | 外层 wlroots screencopy（`wlgrab.cpp`） | 输入桥 → 外层虚拟指针/键盘 → 内层 | uinput；无本地会话时不存在宿主手柄冲突 | 会话的 PipeWire；会话默认 sink 设为 `prism-stream` 或专用 sink |
| 接入本地会话 | KWin zkde_screencast（`kwingrab.cpp`） | uinput → 本地 KWin | uinput | 现有虚拟模式规则 |
| 镜像（兼容） | 现有 | 现有 | 现有 | 现有 |
| 隔离 headless 应用（过渡） | 现有 | 现有 | 现有 | 现有 |

## 4. 配置与数据布局

| 范围 | 位置 | 内容 |
|---|---|---|
| 主机级（M3 起） | `/etc/prism/prism.conf` | 端口、编码器、网络、会话策略、PIN 策略 |
| 主机级状态（M3 起） | `/var/lib/prism/` | 凭据、配对证书、设备↔用户绑定 |
| 用户级 | `~/.config/prism/` | `apps.json`、app-sync 选择、HDR 校准、内层桌面选择等个人偏好 |
| 运行时 | `/run/prism/`（prismd）、`$XDG_RUNTIME_DIR/prism-*`（会话内） | 套接字、会话状态、捕获覆盖文件 |

M2 期间仍是用户级服务，配置留在现有位置；新增配置项在 M2 就按"主机级/用户级"归类命名，减少
M3 迁移的工作量。

## 5. IPC

- 传输：Unix 域套接字（`SOCK_SEQPACKET`），对端身份用 `SO_PEERCRED` 校验。
- 编码：长度受限的 JSON 消息，带协议版本号；每条消息有固定的字段集合，未知字段即拒绝。
- 通道：
  - `prismd` ↔ `prism-session-helper`：特权操作。
  - `prismd` ↔ 会话代理：launch 参数和事件。代理只能操作自己所在的会话。
- 秘密（`rikey` 等）只在 IPC 中传递，不写入命令行、环境变量文件或日志。

## 6. 安全模型

| 信任边界 | 威胁 | 措施 |
|---|---|---|
| 网络 → prismd | 未配对设备、重放、暴力尝试 PIN | 沿用配对证书校验；PIN 限速与锁定；未绑定设备不能建会话 |
| prismd → helper | prismd 被攻破后提权 | helper 接口极小、参数白名单、只为绑定过的用户开会话、不执行任意命令 |
| prismd ↔ 代理 | 代理冒充、跨用户操作 | `SO_PEERCRED`；代理只能访问自己的会话 |
| 会话 → 宿主 | 远程会话获得不应有的权限 | 远程会话是普通用户会话；polkit 规则只放开明确列出的操作（见问题 Q6） |
| 持久化 | 凭据泄露 | `/var/lib/prism` 0700；秘密文件 0600 |
| 远程输入 → 本地座位 | 远程会话的设备授权失败时，Prism 的 passthrough 键鼠挂在 seat0，被本地合成器或登录界面当普通输入消费（S1 R8、S2 R3 实测） | 设备授权不依赖 `uaccess`（T2.10）；输入桥打不开设备时失败关闭，远程输入不得到达本地座位 |

系统单元启用 systemd 加固：`ProtectSystem=strict`、`ProtectHome=yes`（prismd）、
`NoNewPrivileges=yes`、`CapabilityBoundingSet=` 最小化、`RestrictAddressFamilies` 等。

## 7. 兼容的捕获模式

现有 `prism-capture` 取值全部保留。新增 `session`：表示"在用户会话里运行（接入或新建）"。
"Desktop" 和同步应用在新路径稳定后默认使用 `session`；`headless` 保留为隔离应用模式。

## 8. 迁移路径

| 阶段 | 结构 | 说明 |
|---|---|---|
| 现在 | 用户级 `prism.service`，按应用启停的 headless | — |
| M2 | 仍是用户级服务；引入会话对象、策略、`session` 模式、远程会话组成（外层 + 内层）、接入本地会话 | 远程会话在 M2 只能在"本地没有图形会话但用户管理器在运行"时新建（例如开启 linger 或 SSH 登录后），用于提前验证会话模型 |
| M3 | 拆出 `prismd` 和 helper，用 PAM 创建会话；用户级服务转为会话代理 | 提供迁移命令，搬迁配置和凭据；迁移后移除用户级单元，此后只保留系统级部署（ADR-0009） |
| M4 起 | 共存策略、体验打磨 | — |

## 9. 上游同步策略

- 新功能放在新文件：`src/session/`（状态机、策略、IPC）、`src/platform/linux/session/`（logind、
  PAM、systemd 交互）、`contrib/remote-session/`（脚本和单元）。
- 对上游文件（`nvhttp.cpp`、`process.cpp`、`stream.cpp`、`main.cpp`）只加最少的调用点，并在
  调用点注释说明这是 Prism 的接缝。
- 每个里程碑结束时运行一次 `scripts/update-upstream.sh` 演练，评估冲突面。

## 10. 参考

- [gst-wayland-display](https://github.com/games-on-whales/gst-wayland-display)：Wolf 的外层合成器（Smithay，GStreamer 源，DMA-BUF 零拷贝）。
- Games on Whales 的 `images/base-app/build/scripts/launch-comp.sh` 和 `apps/xfce/build/scripts/launch-comp.sh`：内层用 `dbus-run-session` 起 sway/gamescope/XFCE。
- [GNOME Remote Desktop 图形化远程登录 MR 139](https://gitlab.gnome.org/GNOME/gnome-remote-desktop/-/merge_requests/139)：系统守护进程 + GDM headless 登录界面 + handover。
- [KDE Discuss：Plasma 6.8 与 Plasma Login Manager](https://discuss.kde.org/t/plasma-6-8-and-plasma-login-manager/48643)：KDE 计划在 Plasma 6.9 提供独立于会话的 headless 多会话 RDP。
- `kwin_wayland --help`：本机 Plasma 6.7.5 支持 `--virtual --width --height`。

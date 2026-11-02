# S3 由系统单元为用户创建无座位会话（VM 部分）

状态：**VM 部分完成**。NVIDIA 设备、真实声卡、`systemd-homed` 加密家目录、KWallet 解锁没有验证，见"未覆盖"。

## 目的

验证"系统服务以 root 身份通过 PAM 为用户开一个 `Type=wayland`、`Class=user`、无座位、`Remote=yes` 的
logind 会话，并在其中运行桌面"是否可行，收集 helper 设计（ADR-0002）和问题 Q6、Q8、Q15、Q17 的数据。

## 环境

T0.3 的 VM（Fedora 44，Plasma 6.7.5，**SELinux Enforcing**，无 GPU、无声卡）。测试用户 `s3user`（UID 1001，
无密码，未开 linger）。

## 方法

- `/etc/pam.d/prism-remote`：`auth/account` 用 `pam_permit`，`session` 为 `pam_loginuid`、`pam_keyinit`、
  `pam_limits`、`pam_namespace`、`pam_systemd`（最终去掉了 `pam_selinux`，见 F2）。
- 一个以 root 运行的 Python 小工具（ctypes 调 libpam），按顺序 `pam_start`、`PAM_RHOST` 置为
  `prism-remote`、`pam_putenv(XDG_SESSION_TYPE=wayland, XDG_SESSION_CLASS=user)`、`pam_acct_mgmt`、
  `pam_setcred`、`pam_open_session`，之后保持会话直到收到 SIGTERM，再 `pam_close_session`。它由
  `systemd-run` 起为系统服务，与将来的 `prism-session-helper` 的位置一致。

## 结果

| 项 | 结果 |
|---|---|
| 会话属性 | `Type=wayland`、`Class=user`、`Remote=yes`、`RemoteHost=prism-remote`、`Service=prism-remote`、`Seat` 为空、`Active=yes`、`State=active` |
| `user@1001` | 随会话启动，`systemctl --user is-system-running` 为 `running`；会话打开后需要数秒才就绪（一次 8 秒内已就绪，另一次 8 秒时还没有） |
| PipeWire / PulseAudio | 套接字存在，`pactl info` 正常，只有 `auto_null` 空 sink（VM 无声卡，无法判断 ALSA 设备在无座位下的可访问性） |
| 会话进程 | helper 进程被 `pam_systemd` 放进 `session-N.scope`；其日志因此归在该 scope 下，`journalctl -u <helper 单元>` 看不到 |
| 在会话里运行嵌套 Plasma | 通过（Enforcing 下，外层 labwc + 内层 Plasma，与 S1 相同，没有出现 S1 中的 UPower 卡死） |
| 注销清理 | 停止 helper 后会话、`user@1001`、`/run/user/1001` 全部消失，无残留单元和进程 |
| 设备权限（无座位） | `/dev/dri/renderD128` 可读写（模式 666）；`/dev/dri/card1`（`root:video 660`）、`/dev/uinput`、`/dev/input/event*`（`root:input 660`）**不可访问** |

### Q6 polkit 判定（实测）

用同一用户 `s3user` 的两个会话的进程对比（`pkcheck --process`）：

| 动作 | 本地 seat0 会话 | 远程无座位会话 |
|---|---|---|
| udisks2 挂载、NetworkManager 网络控制、colord 创建设备 | authorized | **auth_admin** |
| NetworkManager 个人连接设置 | authorized | auth_self_keep |
| login1 重启、关机、挂起 | authorized | **auth_admin_keep** |
| packagekit 安装包 | auth_admin_keep | auth_admin |

无座位远程会话的判定与 ssh 登录的非本地会话相同。用户可见的后果：在远程会话里 Plasma 开机就弹出
"System policy prevents control of network connections" 的管理员密码框（S4、S2 的截图里都出现了），
挂载 U 盘会要求管理员密码。T3.8 的 polkit 规则必须覆盖至少网络和可移动存储。

## 发现（对 M3 helper 设计的约束）

### F1 `uaccess` 对无座位会话不生效

Prism 的 `61-prism-input.rules` 和 DRI 节点的访问权都靠 `uaccess`（座位上的活动会话），无座位会话拿不到。
`prism-input-bridge` 打不开 Prism 的 evdev 设备，合成器用不了 `card*`。需要为远程会话提供基于用户组、
ACL 或专用 udev 规则的授权（`uinput` 的创建、Prism 的 passthrough 设备节点、`/dev/dri/*`，NVIDIA
真机上还有 `/dev/nvidia*`）。

### F2 helper 的 SELinux 域

PAM 栈里的 `pam_selinux open` 会为下一次 `exec` 设置用户域，而 helper 作为 `systemd-run` 起的服务运行在
`unconfined_service_t`，`exec` 用户进程时得到 `EACCES`（审计日志里没有对应 AVC 记录，因为 auditd 自己也在
报 `dac_override` 拒绝）。去掉 `pam_selinux` 后会话进程（用户管理器）运行在 `unconfined_u:unconfined_r:unconfined_t`。
M3 需要决定 helper 的 SELinux 域和 PAM 栈（要么给 helper 一个可以转换到用户域的受限域并保留
`pam_selinux`，要么明确接受会话运行在 helper 的域）。

### F3 用户管理器的环境不会自动继承会话环境

会话打开后，用户管理器的环境里没有 `XDG_RUNTIME_DIR`，从中启动的单元会回退到 `/tmp/runtime-<user>`（嵌套
Plasma 的 KIO 套接字就出现在那里）。helper 或会话单元启动脚本必须 `systemctl --user import-environment`
或 `set-environment` 设置 `XDG_RUNTIME_DIR`、`XDG_SESSION_ID`、`XDG_SESSION_TYPE` 等。

### F4 就绪等待

`pam_open_session` 返回时 `user@UID` 还没就绪。helper 需要等待用户总线（`/run/user/UID/bus`）和
`is-system-running`，带总超时（原则 4）。

### F5 SELinux 与 PAM 助手的其他限制

- `systemd-run` 的服务如果用 `StandardInput=file:` 指向 FIFO，在 Enforcing 下被拒（`208/STDIN`）。助手不应依赖
  这类输入方式。
- 在 `/` 或家目录权限异常时（见下"VM 缺陷"），`user@UID` 因 `200/CHDIR` 启动失败；该情形下会话本身仍
  然"Active"，调用方必须检查用户管理器是否真的起来，不能只看会话。

## VM 缺陷（已修复，影响 T0.3）

`tests/vm/prism-vm.sh install` 把暂存包以 root 身份解到 `/`，并且保留了包里 `./` 目录项的权限和主机用户的
UID（1000，恰好是客户机的 `prism`）。结果 `/` 变成 `prism:prism 0700`，`/usr`、`/usr/bin` 等也变成
`prism` 属主：其他用户无法执行任何程序，`user@1001` 因 `CHDIR` 失败。已修复为 `tar --owner=0 --group=0
--numeric-owner` 打包、`tar --no-overwrite-dir --no-same-permissions` 解包。

## 未覆盖

- Q8 加密家目录（`systemd-homed`、LUKS 家目录）：未测。
- Q17 KWallet：本实验用 `pam_permit` 认证，没有密码可派生钱包密钥；`sddm-autologin` 的日志显示
  `pam_kwallet5: open_session called without kwallet5_key`，即没有密码就不会解锁。仅 PIN 认证时无法
  自动解锁钱包，与问题清单的假设一致。
- NVIDIA/CUDA/NVENC 设备访问、真实声卡与 WirePlumber：VM 没有。
- 同时存在本地会话时的行为见 S4。

## 对问题清单和 ADR 的更新建议

- Q6：从"待验证"改为"已验证"，结论见上表。
- Q15：VM 里只验证了空 sink 和 PipeWire 可用；真实声卡下的行为待真机。
- Q17：部分验证（见"未覆盖"）。
- ADR-0002 细化：helper 的 PAM 栈、SELinux 域、环境导入、就绪等待（F2–F4）；设备授权（F1）。

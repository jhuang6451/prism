# S4 远程会话存在时本地登录（VM 部分）

状态：**VM 部分完成**（SDDM；没有测 Plasma Login Manager）。

## 目的

用户 X 已有远程会话时，X 在物理座位上登录会发生什么；Prism 能否通过 logind 提前感知；M4 可选的策略。

## 环境

T0.3 的 VM，SELinux Enforcing，SDDM 自动登录。用户 `s3user`：先用 S3 的方法建立远程（无座位）会话，并在
其中运行嵌套 Plasma，然后用 SDDM 自动登录同一用户。

## 步骤与结果

### 实验 A：远程会话用私有 D-Bus（`dbus-run-session`）先运行，再本地登录

- SDDM 没有拒绝，本地会话 59（`seat0`、`Active=yes`）建立，本地 Plasma 正常启动。
- 两套 Plasma 同时运行：远程的嵌套 Plasma（独立总线）和本地 Plasma，各有一份 `kwin_wayland`、
  `plasmashell`、`kded6`、`baloo_file`、`xdg-desktop-portal*`、`ksmserver`。远程画面不受影响。
- 但 `plasma-kwin_wayland.service`、`plasma-plasmashell.service` 等用户管理器里的**共享单元**被本地 Plasma
  占用（MainPID 指向本地的进程），远程那套是绕开单元自己启动的。两者共享同一个 `~/.config`、`~/.local`、
  同一个 systemd 用户管理器。这种状态能跑，但不是安全的结构（配置互写，Q14）。

### 实验 B：本地 Plasma 先运行，再在外层起第二个 Plasma，使用真实用户总线（ADR-0006 的结构）

- `startplasma-wayland` 进程存在、单元显示 active，但**不会启动第二个 `kwin_wayland`**，外层输出一直是黑屏，
  没有任何错误日志。

### 实验 C：远程会话用真实用户总线先运行，再本地登录

- 远程 Plasma（真实总线，`plasma-*` 单元归它）正常；SDDM 照常建立本地会话 70（`seat0`、`Active=yes`），
  本地 `startplasma-wayland` 启动，但**没有出现本地 `kwin_wayland`**：本地座位上没有桌面，同样没有错误提示。
  远程不受影响。

### logind 感知时机

journal 里 `systemd-logind` 在 15:09:03 记录 "New session '70' of user 's3user' with class 'user' and
type 'wayland'"，与本地 `startplasma-wayland` 启动在同一秒，早于任何本地 Plasma 进程。`SessionNew` 信号带有
座位信息，Prism 可以据此在本地桌面起来之前区分"座位上的新会话"和自己的远程会话。（没有单独抓到总线
信号的时间戳，`busctl monitor` 的参数在这个版本上不被接受；用 journal 的时间代替。）

## 结论

1. **同一用户的第二个 Plasma 在同一总线上不会工作，而且是静默失败**（黑屏，无错误）。不论谁先谁后。所以
   "一个用户一个会话"必须由 Prism 的策略保证，不能指望桌面环境拒绝。
2. **SDDM 不会阻止同一用户在有远程会话时本地登录**，所以必须由 Prism 在 `SessionNew` 时处理（T4.1）。
3. 私有总线能让两套 Plasma 并存，但它们共享配置和用户管理器里的单元，不推荐作为共存方案（ADR-0006 保持不变）。
4. M4 的策略选项在数据上都可行：
   - **提示**：`SessionNew` 到达时远程会话里还可以通知用户。
   - **结束远程会话**：在本地 Plasma 起来前（约一秒内）停止远程会话的单元，本地登录即可正常继续（需要验证
     时序，本实验没有做）。
   - **保持并警告**：实验 C 的结果说明这等于本地用户得到一个没有桌面的座位，必须避免。

## 未覆盖

- Plasma Login Manager。
- 手动在登录界面输入密码登录（本实验用自动登录，路径与手动登录的差别在 `sddm-autologin` 与 `sddm` 的 PAM 服务，
  `pam_kwallet5` 在手动登录时有密码可用）。
- "结束远程会话后本地登录是否正常"的完整时序验证（M4 实现时验证）。
- 配置文件互写的具体后果（实验 A 只确认了共享，没有测写入冲突）。

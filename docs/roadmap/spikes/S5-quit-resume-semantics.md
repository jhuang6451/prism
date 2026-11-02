# S5 Moonlight 退出/恢复语义（中期报告）

状态：**服务端部分完成（读代码），客户端部分待做**。客户端部分需要实际操作 Moonlight（Android、PC）和 Iris，
本报告里凡是描述客户端行为的地方都标明了来源，没有实测的不当作结论。

## 目的

确认 ADR-0007 的倾向（`session` 模式下"退出 = 断开，会话保留；应用退出不影响会话"）是否可行，
以及原版 Moonlight 与 Iris 是否需要配合修改。

## 方法

阅读服务端实现：`src/nvhttp.cpp` 的 `serverinfo`、`launch`、`resume`、`cancel`，`src/process.cpp` 的
`proc_t::running`、`terminate`、`prism_capture_end`，`src/rtsp.cpp` 的 `terminate_sessions`。

## 服务端现状

| 请求 / 事件 | 现有行为 | 位置 |
|---|---|---|
| `serverinfo` | `currentgame` 为 `proc.running()` 返回的应用 ID，`state` 为 `PRISM_SERVER_BUSY`（>0）或 `PRISM_SERVER_FREE` | `nvhttp.cpp` serverinfo |
| `launch` | 只要 `proc.running() > 0` 就返回 400 "An app is already running on this host"，**不区分客户端，也不区分应用 ID** | `nvhttp.cpp` launch |
| `resume` | 要求 `running() > 0`，否则 503 "No running app to resume"；**不检查应用 ID，也不检查是不是原来的客户端**，任何已配对设备都能接入 | `nvhttp.cpp` resume |
| `cancel` | 终止所有 RTSP 会话，若有应用在运行则 `proc.terminate()`（进程组退出，`prism_capture_end()` 拆除 headless/virtual 捕获），并还原显示配置 | `nvhttp.cpp` cancel |
| 串流断开（没有 `cancel`） | 只结束 RTSP 会话；应用继续运行，`serverinfo` 仍是 BUSY，之后可以 `resume` | `rtsp.cpp`、`stream.cpp` |
| 应用自己退出 | 下一次 `running()` 轮询发现后调用 `terminate()`，捕获模式随之拆除；`auto-detach` 且 5 秒内正常退出的命令被视为"分离命令"，继续算作运行中 | `process.cpp` running |
| 其他结束应用的路径 | Web UI 关闭应用、取消配对（`confighttp.cpp` 调用 `terminate`） | `confighttp.cpp` |

## 对 ADR-0007 的含义

1. **"断开保留应用"已经是现状**：不发 `cancel` 的断开不会结束应用，headless 会话也还在。`session` 模式要保留的
   正是这一点，服务端不需要为它改什么。
2. **"退出"在协议里就是 `cancel`**。要让"退出 = 断开，会话保留"成立，只能在服务端改 `cancel` 的含义：
   `session` 模式下 `cancel` 结束串流并让"当前附着的入口"变成空闲，但**不结束会话**。
3. **切换入口依赖 `running()` 的定义**。现在 `launch` 的拒绝条件是 `running() > 0`。会话模型里应把
   `running()` 对 `session` 模式解释为"当前有入口附着在串流上"：`cancel` 之后返回 0，`serverinfo` 显示 FREE，
   客户端就能 `launch` 另一个入口，由服务端按策略 attach 到同一个会话。这与架构文档 3.8 的设计一致，
   **原则上不需要改客户端**，因为客户端只看 `currentgame` 和 `launch`/`resume`/`cancel` 的返回值。
4. **安全相关**：`resume` 不校验客户端身份。M3 引入设备↔用户绑定后，`resume` 和 `launch` 都必须校验
   "该设备是否绑定到这个会话的用户"，否则任何已配对设备都能接入别人的会话（对应 T3.4）。

## 客户端行为（未实测，来源为对 Moonlight 的了解，必须实测确认）

- 普通退出串流（关闭窗口、返回键）只断开，不发 `cancel`；菜单里的 "Quit Session"（Moonlight PC 的
  Ctrl+Alt+Shift+E 等）才发 `cancel`。
- 点击另一个应用，而当前有应用在运行时，客户端会提示先退出当前应用，确认后发 `cancel` 再 `launch`。
- `serverinfo` 的 `currentgame` 决定列表里谁显示为"运行中"，点击它会走 `resume`。

## 待实测

用 VM 里的 Moonlight flatpak 命令行（`list`、`quit`、`stream`）和真实客户端验证：

1. `cancel` 后 `currentgame` 变 0，客户端是否正常允许 `launch` 新入口。
2. `session` 模式下 `cancel` 不结束会话时，客户端的提示文案是否误导（例如"应用将被关闭"）。
3. 原版 Moonlight（Android、PC）与 Iris 在"同一应用退出后立刻 `launch` 另一个"流程里的表现。
4. 断网后重连是走 `resume` 还是重新 `launch`。

## 对 ADR-0007 的暂时建议

倾向保持：`session` 模式下 `cancel` 默认等于断开，可配置为同时注销；应用退出不影响会话。
服务端实现路径清楚，主要风险在客户端文案，需要上面的实测确认。**ADR-0007 保持"提议"状态。**

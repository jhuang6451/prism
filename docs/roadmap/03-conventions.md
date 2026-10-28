# 03 项目规范

本规范继承原项目（Sunshine/LizardByte 的贡献规范、`AGENTS.md`、`docs/contributing.md`）并按 Prism
的实际做法整理补充。`AGENTS.md` 是给编码代理的权威摘要；两者冲突时以 `AGENTS.md` 为准，并应
同步修正本文。

## 1. 仓库

| 远端 | 地址 | 用途 |
|---|---|---|
| `origin` | `https://github.com/atgehrhardt/prism` | Prism 原仓库（本仓库的上游） |
| `fork` | `git@github.com:jhuang6451/prism.git` | 用户的 fork，本规划的开发仓库 |
| Sunshine | `https://github.com/LizardByte/Sunshine` | 最上游，通过 `scripts/update-upstream.sh` 同步 |

**硬性规则：**

- 绝不在 LizardByte 组织下创建 issue 或 PR。
- 只有用户明确要求时才创建 issue 或 PR，并且只在 `jhuang6451/prism` 上创建；没有明确要求时，
  也不在 `atgehrhardt/prism` 上创建。
- 只有用户要求时才 commit 或 push。

## 2. 分支、提交与协作

- 分支从 `master` 拉出，命名 `<type>/<主题>`，例如 `feat/session-state-machine`、`spike/s1-nested-plasma`、
  `docs/roadmap`。一个任务卡片对应一个分支。
- 提交信息使用 [Conventional Commits](https://www.conventionalcommits.org/)，与历史一致：
  `feat`、`fix`、`docs`、`build`、`ci`、`chore`、`test`、`refactor`，常用 scope 有 `linux`、`cmake`、
  `deps`、`l10n`，本规划新增 `session`、`prismd`、`agent`。PR 标题同样必须符合格式（squash 合并，
  `.github/semantic.yml` 只检查标题）。
- 提交信息结尾带署名行（由当前会话提示给出），例如：

  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

- PR 描述结尾使用会话提示给出的署名行。
- 一个提交只做一件事；行为变更、测试、文档放在同一个提交或同一个 PR 里，不拆到以后。

## 3. 语言

| 内容 | 语言 |
|---|---|
| 代码、注释、doxygen | 英文 |
| 用户文档：`README.md`、`docs/*.md` | 英文 |
| Web UI 文案、日志 | 英文 |
| 提交信息、PR | 英文 |
| 规划文档：`docs/roadmap/` | 中文 |

本地化只修改 `src_assets/common/assets/web/public/assets/locale/en.json`，键按字母排序；不修改
`en-US` 等英文变体或其他语言文件。品牌名（Prism、Sunshine、LizardByte、NVIDIA 等）不翻译。

## 4. 构建

- 构建目录必须以 `cmake-build-` 开头（`.gitignore` 已覆盖）。
- 测试程序名为 `test_prism`，位于构建目录的 `tests/` 下。
- **Linux（本机 Fedora）**：宿主机没有 boost/gtest，用 podman 按 `.github/workflows/fedora-smoke.yml`
  的方式构建：
  1. `bash scripts/checkout-linux-submodules.sh`（一次即可）。
  2. 用 `FROM registry.fedoraproject.org/fedora:44` 的 Containerfile，安装与 `fedora-smoke.yml` 相同的
     依赖，生成依赖镜像（Containerfile 放在仓库外）。
  3. `podman run --rm --security-opt label=disable -v "$PWD:/src" -w /src <镜像> bash <脚本>`，脚本内
     执行工作流里的 cmake 配置（`-B cmake-build-podman -G Ninja -DBUILD_TESTS=ON -DBUILD_WERROR=ON
     -DPRISM_ASSETS_DIR=share/prism -DPRISM_ENABLE_CUDA=OFF ...`）、`cmake --build --parallel`，再
     `cd cmake-build-podman/tests && xvfb-run --auto-servernum ./test_prism`。
  4. 已知与本规划无关的失败：`DownloadFileTests/*`（容器无网络）；约 9 个 GPU/硬件测试会跳过。
- **Windows**：用 msys2 ucrt64，命令前加
  `C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c`。
- Web UI：`cmake -B cmake-build-prism -G Ninja -S . --target web-ui && ninja -C cmake-build-prism web-ui`，
  或 `npm run dev`。

## 5. C/C++

- 标准：C++23（`CMakeLists.txt`）。
- 格式：遵守 `.clang-format`，用 `scripts/lint.sh` 固定的 clang-format 20.1.8 检查。
- 命名（沿用现有代码）：函数和变量 `snake_case`；结构体/类型 `_t` 后缀；枚举 `_e` 后缀；按模块分
  命名空间（`proc`、`config`、`platf` 等）；字符串字面量用 `using namespace std::literals` 的 `""s`、`""sv`。
- 日志：`BOOST_LOG(level)`，Prism 自己的逻辑加 `[prism]` 或模块标签前缀（如 `[session]`、`[prismd]`）。
  日志不得包含密钥、PIN、`rikey`、证书私钥。
- **doxygen 必须完整，否则构建失败：**

  ```cpp
  /**
   * @brief Describe the function, structure, etc.
   *
   * @param my_param Describe the parameter.
   * @return Describe the return.
   */
  ```

  行内注释用 `///< ...`，不用 `/**< ... */`。每个文件有 `@file` 和 `@brief`。
- 新功能放在新模块（见[架构文档第 9 节](02-architecture.md#9-上游同步策略)）；改上游文件只加最少的
  接缝调用。
- **可测试性**：决策逻辑写成纯函数（输入快照 → 输出动作）；与系统交互（logind、systemd、PAM、
  文件系统）通过接口注入，测试里替换为假实现。
- 错误处理：失败返回错误码或 `std::optional`/`std::expected` 并记录日志；不让异常跨越模块边界。
- C 辅助程序（如 `prism-input-bridge.c`）同样需要 doxygen 注释和 clang-format。

## 6. Shell 脚本

- 使用 bash。新脚本以 `set -euo pipefail` 开头；修改旧脚本时保持其现有风格（部分为 `set -u`）。
- 必须通过 `shellcheck` 0.10（`scripts/lint.sh`）；确需禁用的规则要写明原因。
- 函数文档使用 `## @brief`、`## @param`（与 `build-headless-compositor.sh` 一致）。
- 公共逻辑放进被 `source` 的公共库（如 `prism-headless-common.sh`），并写 `# shellcheck source=` 指引。
- 所有路径根（`/proc`、`/sys`、cgroup、运行时目录）都可以通过环境变量覆盖（如 `PRISM_PROC_ROOT`、
  `PRISM_SYS_ROOT`），以便测试。
- 永远不对来自客户端或配置的字符串做 `eval`；命令以参数数组形式传递。
- 日志写到 `~/.local/state/prism-*.log`（用户级）或 journal（系统级）。

## 7. 运行时工程原则

这些原则来自现有 headless/virtual 实现，新代码必须遵守：

1. **失败即关闭（fail closed）。** 条件不满足就明确失败，绝不悄悄退回到捕获其他屏幕、其他会话
   或其他后端。
2. **只动自己拥有的资源。** 操作前验证所有权（单元名、cgroup、套接字 inode、状态文件中的会话 ID）；
   没有记录的东西不碰（例如未记录的物理输出状态）。
3. **原子发布状态。** 状态文件先写临时文件再 `rename`；文件带版本号，解析严格，字段数量和取值都校验。
4. **有界的就绪等待。** 一个阶段一个总超时，不做无限重试；超时后完整回滚。
5. **幂等的协调（reconcile）。** 启动前和退出后都运行清理，可以重复执行；确认存在但无法清理的资源
   要阻止启动并报告。
6. **对称的建立与拆除。** 拆除按建立时记录的内容进行，不重新推断。
7. **生命周期操作串行化。** 同一资源的建立和拆除在同一把锁下进行。
8. **最小特权。** 网络面不持有 root；特权操作集中在接口最小的辅助进程里。

## 8. systemd 单元

- 名称以 `prism-` 开头；按会话实例化的单元使用模板（`prism-outer@<id>.service`）。
- 会话内单元使用 `KillMode=control-group`，由会话目标统一停止。
- 需要清理的服务配置 `ExecStopPost=` 调用协调脚本。
- 系统单元启用加固选项（见[架构文档第 6 节](02-architecture.md#6-安全模型)），并在单元文件里注释
  每个放开的权限的用途。
- 环境文件放在运行时目录（`%t`），权限 0600。

## 9. 配置项

- 键名小写加下划线；Prism 自有的键以 `prism_` 开头（已有的 `app_sync_*` 保持不变）。
- 新增配置项必须同时修改（`ConfigConsistencyTest` 和 `LocaleConsistencyTest` 会检查）：
  1. `src/config.cpp`（用 `string_f`、`int_f`、`bool_f`、`generic_f` 等标准解析函数；自定义解析函数
     会绕过一致性检查，不要用）；
  2. `src_assets/common/assets/web/config.html` 对应标签页的选项；
  3. `docs/configuration.md`（`### key` 小节，顺序与 config.html 一致）；
  4. `en.json` 的 `config.*` 文案。
- 默认值在文档里写清楚；改变默认行为要在 PR 描述里说明。
- 按"主机级 / 用户级"归类（见[架构文档第 4 节](02-architecture.md#4-配置与数据布局)），为 M3 迁移做准备。

## 10. 测试

- 框架：Google Test。单元测试在 `tests/unit/test_<模块>.cpp`；集成测试在 `tests/integration/`。
- Shell 脚本测试写成 `tests/integration/test_*.sh`，用假的 `systemctl`、`pactl`、`wlr-randr` 等桩和可
  覆盖的路径根运行，并在 `tests/integration/test_external_commands.cpp` 里注册为 gtest 用例。
- Python 测试（`tests/integration/test_*.py`）需通过 flake8。
- **新增或修改的代码要有测试，目标是变更行 100% 覆盖。** 构建默认带 gcov，可在容器内查看覆盖率。
  只有 CI 无法运行的代码（GPU、真实硬件、需要 logind/PAM 的路径）才可以用 gcovr 排除标记，并写明
  原因；这类代码要在 [05 文档](05-risks-and-testing.md)的手动或 VM 测试清单里有对应条目。
- 状态机和策略这类纯逻辑要穷举测试所有状态和输入组合。
- 测试不得依赖宿主机的真实会话、真实 D-Bus 或网络。

## 11. 文档

- 行为变更必须同步更新 `README.md`、`docs/configuration.md` 等用户文档。
- 要发布到 doxygen 站点的新用户文档需加入 `docs/Doxyfile` 的 `INPUT`。
- 架构决策写入 [decisions.md](decisions.md)（模板见该文件）；推翻旧决策时新增一条并把旧的标为
  "已被取代"，不删除历史。
- 每完成一个任务，更新 [04-tasks.md](04-tasks.md) 的状态和 [README](README.md) 的里程碑状态；
  解决了问题清单里的条目，就更新 [05 文档](05-risks-and-testing.md)。
- 实验报告放在 `docs/roadmap/spikes/<编号>-<主题>.md`，包含：目的、环境（发行版、桌面版本、GPU、
  驱动）、步骤、结果数据、结论、对 ADR 的建议。

## 12. 完成标准（Definition of Done）

一个任务只有满足以下全部条件才算完成：

- [ ] 任务卡片的验收标准全部满足。
- [ ] 代码遵守本规范（格式、命名、doxygen、工程原则）。
- [ ] 新增和修改的代码有测试，变更行覆盖率达到目标或有书面的排除理由。
- [ ] `scripts/lint.sh` 通过（可用 `git config core.hooksPath .githooks` 在 push 前自动运行）。
- [ ] podman 构建（`-DBUILD_WERROR=ON`）和 `test_prism` 通过（已知无关失败除外）。
- [ ] 需要硬件或 VM 的测试点已按 05 文档执行并记录结果。
- [ ] 用户文档和规划文档已更新。
- [ ] 没有引入未记录的配置项、状态文件或系统单元。

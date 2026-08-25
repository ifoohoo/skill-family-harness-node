# 变更日志

<!-- release-skill:changelog:start version=0.11.0 locale=zh-CN baseline=sha256:51776685eead52118cc98ee47b8ac8990650a493d60567002133ff74e1c779c6 -->
## [0.11.0] - 2026-08-25

Harness 0.11.0 增加原始字节子进程输出 sink，并暴露真实宿主验证所需的受保护根身份。

### 新增

- 为 superviseProcess 增加独占、禁止跟随符号链接的 stdout/stderr 原始字节 sink，并等待流关闭、排队写入、fsync 与 close。
- 继续把既有 bound-read 机制作为唯一根目录与成员读取权威。

### 变更

- 把此前准备好的宿主 Profile 闭包并入 0.11.0 三包锁步交付。

### 升级说明

原始 sink 只提供机制，不建立第二个进程 runner、收据状态机或宿主专属策略。调用方必须在整个调用期间独占 sink 命名空间；句柄保护不证明 pathname 或根目录身份始终不变。
<!-- release-skill:changelog:end version=0.11.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.10.0 locale=zh-CN baseline=sha256:d8e27cce0c3e97b7d10b6f88fad4df66c8d527d3d6a2fb165f8c909612ff9f19 -->
## [0.10.0] - 2026-08-24

Harness 0.10.0 增加规范入口，复用既有宿主机制，并从真实目录提供同级适配器只读验证。

### 新增

- 新增 skill-family-harness-node/quickstart-profile 与 skill-family-harness-node/rename-directory-no-replace 规范导出。
- 复用 filesystem-root binding、严格不替换发布、原子替换和既有构建摘要，支持 Kit 的本地宿主 install/update。
- 新增 `verifyPeerAdapterDirectories`，重新枚举 peer 根目录，验证共同闭包、逐字节摘要、标准 manifest 和完整 logicalMappings，不写入目录。

### 变更

- 历史 candidate 导出继续作为同源迁移别名，机制登记表不变。
- validate-many-by-schema-id 及错误语义不变；受管 Bundle 让历史与规范 Schema ID 共用同一 validator。

### 升级说明

消费者应把三个包的精确 pin 更新到 0.10.0，并把历史 candidate 导入和 Schema ID 一次迁移到规范身份。低层不替换原语仍不同于稳定 fixed-set-publication API，消费者按所需合同选择。
<!-- release-skill:changelog:end version=0.10.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.9.0 locale=zh-CN baseline=sha256:7f3ff503831b8fb52f20293a4af4003ff116b7f9d820a0c2da1bd34a0b261248 -->
## [0.9.0] - 2026-08-24

Harness 0.9.0 新增稳定身份绑定读取与固定集合不替换发布，并携带固定四平台原生闭包。

### 新增

- 新增 createFilesystemRootBinding 与 readFileBound，使用句柄相对不跟随符号链接获取，并支持可选字节摘要保护。
- 新增稳定 fixed-set-publication 子路径，使用原生不替换发布并保留终态 indeterminate 回执。
- 在既有 Quickstart dispatcher 中增加 validate-many-by-schema-id candidate 机制。

### 变更

- 身份保护删除继续排除，既有 21 项能力登记保持不变。

### 升级说明

三个 Foundation 包必须精确锁定 0.9.0。批量校验与 Quickstart Bundle 仍为 candidate；文件系统绑定与固定集合发布为 stable。
<!-- release-skill:changelog:end version=0.9.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.8.4 locale=zh-CN baseline=sha256:e8d7be87a26ef296ee4ae27e2325ac8ff1dc33fc69e5f4a65520656f9860bb91 -->
## [0.8.4] - 2026-08-24

随 Foundation 0.8.4 锁步升版；Harness 不新增机制或公共 API。

### 变更

- 包版本与 Contracts、Engineering Kit 一同升至 0.8.4。
- 21 项 Harness 能力与公共导出保持不变；source-authority receipt 校验归 Contracts。

### 升级说明

消费者必须把三个 Foundation 包精确锁定到 0.8.4；Harness API 无需迁移。
<!-- release-skill:changelog:end version=0.8.4 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.8.3 locale=zh-CN baseline=sha256:b84695a286aca7c1a9f90844f8682cde07d32276cebc11583568bc5ef11753fc -->
## [0.8.3] - 2026-08-23

路径收容现在可处理一次精确的锚点并发删除，同时保持越界检查失败关闭。

### 变更

- 第二次 ENOENT 与所有非 ENOENT 错误仍失败关闭；符号链接替换、根目录外目标和既有收容检查仍全部拒绝。
- 测试钩子只留在 paths 模块的测试表面，不新增公共导出、通用重试策略、锁层、账本或 runner。
- 包版本与 Contracts、Engineering Kit 一同升至 0.8.3。

### 修复

- 当另一进程删除已选锚点，导致该锚点的 realpath 返回 ENOENT 时，resolveContained 仅重新计算一次最深既存祖先。

### 升级说明

消费者必须把三个 Foundation 包精确锁定到 0.8.3。并发获取锁时，即使当前持有者删除已选锁文件锚点，也能完成一次安全重求；Harness API 无需迁移。
<!-- release-skill:changelog:end version=0.8.3 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.8.2 locale=zh-CN baseline=sha256:748d79d51dcabe6193332e60ec69a6eb26fd914ad3555f6e924b78db71016192 -->
## [0.8.2] - 2026-08-23

固定的候选机制桥接新增 read-file-strict，直接公开既有严格读取机制。

### 新增

- invokeFoundationMechanism 新增 read-file-strict；参数闭合为 root、path、encoding 和 expectedSha256。
- UTF-8 内容返回字符串，二进制内容返回标准的 JSON 安全 Buffer 形态。

### 变更

- 路径收容、文件读取、摘要复验和失败分类全部转发给 readFileStrict，不新增第二套读取算法。
- 包版本与 Contracts、Engineering Kit 一同升至 0.8.2。

### 升级说明

候选消费者必须把三个 Foundation 包精确锁定到 0.8.2，再重建受管 Bundle。直接调用继续返回 SFC2004 与 details.kind；JSON CLI 只承诺成功退出码 0 和拒绝退出码 2。
<!-- release-skill:changelog:end version=0.8.2 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.8.1 locale=zh-CN baseline=sha256:904de77446dbc69ef7e70cdda0020fd98632a6e704754f6ed57e136764e3112e -->
## [0.8.1] - 2026-08-22

宿主把 Harness 打包进单文件适配器后，报告渲染器仍能保留 Harness 自身的包版本。

### 变更

- 包版本与 Contracts、Engineering Kit 一同升至 0.8.1。
- Harness 的 21 项能力、报告合同和公共导出均保持不变。

### 修复

- 用静态 JSON import 替换报告渲染器运行时基于 import.meta.url 的包清单查找，使 esbuild 内联 Harness 版本，不再读取宿主适配器的包元数据。
- 为 REPORT_RENDERER_VERSION 新增源码运行与第三方单文件 bundle 回归测试。

### 升级说明

把报告渲染器打包进宿主适配器的消费者必须精确锁定 Harness 0.8.1；报告 API 与渲染器身份保持不变，从 0.8.0 升级不需要迁移 API。
<!-- release-skill:changelog:end version=0.8.1 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.8.0 locale=zh-CN baseline=sha256:83f13f0b9f624ee26865c5d1bb0295ecd8cbaa64c581f066b53012eadf1dc040 -->
## [0.8.0] - 2026-08-21

随 Foundation 0.8.0 线锁步升版；Harness 机制表面不变。

### 变更

- Contracts 1.7.0 新增 Project Profile 合同，但 Harness 能力和导出机制均不变。

### 升级说明

消费者保持 Harness 机制锁定；Project Profile 校验归 Engineering Kit/Profile SPI 负责，不新增 Harness 机制。
<!-- release-skill:changelog:end version=0.8.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.7.0 locale=zh-CN baseline=sha256:b3c207e2dd4134b75e5219842ee64a43c6da964763a86f7e360f0d0b60667516 -->
## [0.7.0] - 2026-08-21

随 Foundation 0.7.0 线锁步升版；薄机制运行时不变。

### 变更

- 能力无变更——HARNESS_CAPABILITIES 保持 21 项，全部导出机制（原子受收容写、路径收容、严格权威读取、资源闭包、摘要、有界子进程监督、URL 凭证脱敏）保持 0.6.0 合同；包版本随 Foundation 线锁步，因为三个叶子包共用同一公开版本坐标。

### 升级说明

0.7.0 不携带任何 harness 表面变更。消费者保持既有锁定；harness computeResourceClosure 的资源闭包与 engineering-kit 0.7.0 引入的 Kit 计划闭包仍然形状不同、用途不同，二者不能互换。
<!-- release-skill:changelog:end version=0.7.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.6.0 locale=zh-CN baseline=sha256:6a4a94f692909faf2090445638988b27841a8bca67a85fd7812864d656299175 -->
## [0.6.0] - 2026-08-21

本版新增有界子进程监督（FND-ADR-012），补齐 Foundation 严格权威读取路径与受收容嵌套目录准备（FG-1），新增落盘前 URL 凭证脱敏（FG-2），HARNESS_CAPABILITIES 从 18 项增至 21 项。

### 新增

- 新增 superviseProcess 与 validateTimeoutPolicy、WATCHDOG_REASONS、TERMINATION_REASONS、PROCESS_STATUSES、ENVELOPE_GUARANTEES（FND-ADR-012）：单次有界 spawn、显式事件判活、消费者提供的超时策略、对进程组 SIGTERM→宽限→SIGKILL、单一闭集枚举终止信封；机制从不重启被监督进程，也从不持有超时数值。
- 新增 readFileStrict（FG-1）：严格写入路径的读取孪生体——先收容、拒绝符号链接（O_NOFOLLOW）、在已打开句柄上复核常规文件身份（dev/ino），并返回所读精确字节的 sha256 摘要回执；可选 expectedSha256 内容守卫在任何交付前失败关闭。
- publishFileExclusive 新增 createParents 选项（FG-1）：父目录链缺失部分在收容层内准备为真实目录，每个中间条目都复核为真实目录；符号链接组件仍被拒绝，任何条目都不被替换。
- 新增 redactUrlCredentials 与 REDACTED_URL_PLACEHOLDER（FG-2）：任何 URL 的 userinfo 部分在数值到达磁盘或日志前剥离；不可解析输入退化为不透明占位符，绝不泄漏到输出。
- 在 estimateTokens 旁转导出 contracts 拥有的 token 估算消费合同（consumeTokenEstimate、consumeTokenEstimateStrict 及伴生常量），并承载权威词元估算器 estimateTokens 与 skill-family-token-estimate CLI（审计整改 C1）。

### 变更

- HARNESS_CAPABILITIES 从 18 项增至 21 项（新增 supervise-process、strict-read、url-credential-redaction）；严格写入路径的不替换、字节验证回执语义保持不变。
- 业务语义、重试/重启策略、预算阈值与「哪些值是 URL」的判定继续归消费者所有；harness 只拥有机制。

### 升级说明

0.6.0 是 Foundation 能力补齐线。publishFileExclusive 的 createParents 选项是按 2026-08-19 纪律的 Foundation 侧 profile 行为变更；需要受收容嵌套发布的消费者必须精确锁定 0.6.0。
<!-- release-skill:changelog:end version=0.6.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.5.0 locale=zh-CN baseline=sha256:003472359596147fe26ad2ba53a82303a217c28cb89a6afacdaa955793001f71 -->
## [0.5.0] - 2026-08-16

本版在稳定 Harness 面上新增声明读取表面断言（FND-ADR-010）与结构化表面扫描器（FND-ADR-011），HARNESS_CAPABILITIES 从 16 项增至 18 项，并引入三个经依赖评审的运行时库。

### 新增

- 新增 assertDeclaredReadSurface（FND-ADR-010）：不执行、仅语法面的断言，声明模块集内的每个 node:fs 具名导入必须落在消费者声明的读取表面内，违规词汇表为闭集，返回冻结的 declared-read-surface-result 信封。
- 新增 scanSurfaceStructured（FND-ADR-011）：scanSurface 的结构化兄弟。IP 形候选统一进入单一标准解析入口（ipaddr.js），按消费者声明的 CIDR 批准，不可解析即失败关闭；坐标（scoped 与非 scoped）、注册表与主机均需消费者声明批准；格式适配器（pnpm-lockfile 经 @pnpm/lockfile.fs + yaml AST 注释区域，tree-json 经 JSON.parse）结构化解析且无位置级豁免；二进制与符号链接策略失败关闭。闭集九规则词汇表经 structured-scan-violation 机制错误的 details.rule 承载。
- 为结构化扫描器引入三个经依赖评审的运行时库，评审按 FND-ADR-006 第 4 节类比执行（依赖闭包预审，执行者自审，独立复核另行安排）：ipaddr.js 2.5.0（MIT，零依赖）、yaml 2.9.0（ISC）、@pnpm/lockfile.fs 1001.1.35（MIT，18 个传递依赖全部在批准公共坐标内）。
- HARNESS_CAPABILITIES 从 16 项增至 18 项，覆盖两个新机制。

### 变更

- 显式说明两个策略文档与工作区私有 leak 策略的关系：工作区私有的 leak-policy.json 实例文档既不是 surface-scan-policy 或 structured-scan-policy schema 的子集、也不同构、更不是迁移目标——两类文档按设计共享规则词汇与失败关闭语义，但字节级形状相互独立，不得比较兼容性。scanSurface 是执行内核通用化投影：同一机制族的公开、消费者参数化形态，自身不解释任何私有身份、路径或批准清单。
- 如实记录依赖评审决策的成本：harness tarball 随 @pnpm/lockfile.fs 闭包扩大；薄运行时属性从零第三方运行时依赖变为三个经评审依赖；pnpm-lockfile 适配器会在 OS 临时目录写一份临时锁文件副本（用后即删）。engineering-kit 的离线消费者验证门随之把第三方闭包推导从单包闭包机械扩展为三个 Foundation 包的完整生产闭包（真实身份去重、npm: 别名感知、range-scoped override selector），使评审决策持续对着真实安装字节被验证。
- 保持机制纯度：不执行被扫描文件、无模型调用、无网络；永不跟随符号链接。

### 升级说明

0.5.0 是 FND-ADR-010/011 harness 线。机制导入使用 HARNESS_CAPABILITIES 公布的稳定能力名；structured-scan 策略必须通过契约规格 1.5.0 的 structured-scan-policy 契约校验。
<!-- release-skill:changelog:end version=0.5.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.4.0 locale=zh-CN baseline=sha256:dafaa1100bf6163a28d53fc982a30e8325d8bd3cab43bbb6daf2e6002befa2c1 -->
## [0.4.0] - 2026-08-16

本版在稳定 Harness 面上新增五项有限封闭语义机制与 token-lock 原语（FND-ADR-009），HARNESS_CAPABILITIES 从 9 项增至 16 项，并随附 rename-directory-no-replace 原生预编译候选。

### 新增

- 新增五项有限封闭语义机制（FND-ADR-009）：冻结基线物化 + contentGuard、通用只读 chokepoint、策略化表面扫描、确定性 token 上界估算与通用上限守卫。
- 新增 token-lock 原语：独占 token 文件发布与摘要复验。
- HARNESS_CAPABILITIES 从 9 项增至 16 项，覆盖上述新机制。
- 新增 rename-directory-no-replace 候选：覆盖 darwin-arm64、darwin-x64、linux-arm64-gnu、linux-x64-gnu 的原生预编译 no-replace 目录改名 addon，附带发布 receipt 与 SBOM。

### 变更

- 保持 0.3.0 交付的 Quickstart Profile v2 Task/Result 交换校验：path-backed output 与 evidence 重算真实字节、拒绝重复 Resource id、evidence 精确绑定逐项复验。
- 方法选择、重试策略与领域结果解释继续归消费者所有。

### 升级说明

0.4.0 已发布到 npm 与 public 镜像仓。candidate 子路径必须精确锁定 0.4.0；机制导入使用 HARNESS_CAPABILITIES 公布的稳定能力名。
<!-- release-skill:changelog:end version=0.4.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.3.0 locale=zh-CN baseline=sha256:b635e4170d3f7ac634e59f612939683ab04727f87101790d9a9f5613ea38fcfe -->
## [0.3.0] - 2026-08-12

本源码候选版把 Quickstart Profile Harness 更新为 v2 Task/Result 交换校验，同时不接管消费者语义。

### 新增

- 对 path-backed output 与 evidence 重算真实字节摘要，并拒绝 observation、output、evidence 之间重复的 Resource id。
- 逐项复验 operation 身份、Task digest、run/stage/attempt 字段和 evidence 精确绑定集合。

### 变更

- 替换与 0.2.1 不兼容的 candidate 面；仍依赖 v1 的消费者必须继续精确锁定 0.2.1。
- 方法选择、重试策略与领域结果解释继续归消费者所有。

### 升级说明

0.3.0 当前只是本地、未发布的源码候选。candidate 子路径必须精确锁定包版本，采用前需更新 v1 交换生产方。
<!-- release-skill:changelog:end version=0.3.0 locale=zh-CN -->


<!-- release-skill:changelog:start version=0.2.1 locale=zh-CN baseline=sha256:2f2c74ab9dcf0f1a84872743bf203eb43d15c5e722e823e670e8d81ca5f7de65 -->
## [0.2.1] - 2026-08-10

本版新增 Quickstart Profile 候选交换辅助函数，并为包发布文档提供完整英文版与简体中文版。

### 新增

- 新增候选辅助函数，用于创建并复验 observation Resource、构造 Task、封装 Result，并在 Result 未精确绑定 Task 与关联字段时失败关闭。
- 新增完整的英文与简体中文包文档，并补充智能体快速参考章节。

### 变更

- 使用同一份双语版本化说明源管理当前 README 与 CHANGELOG 的发布区域。
- 项目 NOTICE 与 Apache-2.0 LICENSE 分开分发。

### 升级说明

候选辅助函数不改变稳定 Harness API，也不引入生命周期、重试、编排、模型调用、网络或 Git 写入语义。
<!-- release-skill:changelog:end version=0.2.1 locale=zh-CN -->

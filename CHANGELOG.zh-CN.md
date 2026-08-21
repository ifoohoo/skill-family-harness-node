# 变更日志

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

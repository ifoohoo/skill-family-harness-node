<!-- release-skill:safe-first-command -->
<!-- release-skill:external-write-boundary -->

> English version: [README.md](./README.md)

# skill-family-harness-node

<!-- release-skill:release-version: 0.22.0 -->

Contracts 机制协议的**唯一默认 Node 实现**。这是一个薄运行时（thin runtime）：只实现机制协议，不引入业务语义，不做第二语言实现。

<!-- release-skill:managed:start id=latest-release -->
**0.22.0** (2026-09-18)

Harness 0.22.0 通过三个包根导出增加多路径普通文件的应用、恢复与材料清理机制，并给持久状态底座增加有界的锁观察与锁恢复扩展。

**新增**

- 增加 `applyFileSet`、`recoverFileSet`、`pruneFileSetRecovery` 三个 `skill-family-harness-node` 包根导出，组合既有的严格单文件原语、绑定读取和持久状态底座。
- 增加 `inspectStateStoreLock` 与 `recoverStateStoreLock`，调用方可以观察锁状态并修复被中断的状态底座操作，而不清理其内部文件。

**变更**

- 记录整组前检、逆操作、严格同步和逐路径未知事实，同时保持调用方持有的领域验证只读。

**升级说明**

三个 Foundation 包须一起精确锁定到 0.22.0。恢复前调用方必须停止旧参与者并建立外部排他维护区间；领域判定、业务计划和清理授权仍由调用方负责。本机制不新增第二套日志或锁算法、不扩大为目录操作，也不在 darwin/arm64 APFS 之外承诺平台资格。
<!-- release-skill:managed:end id=latest-release -->

## 解决的问题

Contracts 定义了「应当如何」，Harness 把它在 Node 运行时变成「可以安全复用」的机制。多个技能族项目如果各自实现路径收容、原子写、资源闭包、报告渲染、宿主接入与状态底座，会出现安全边界不一致、行为漂移。Harness 把这些业务中立机制收口成一份默认实现，调用方按需取用。

## 核心心智模型

Harness 消费 `skill-family-contracts`（工作区依赖），复用其方言路由的 Ajv validator、Kernel Protocol、冻结错误码与 fixture；不复制协议定义，不重新解释 Schema。它只实现机制：Schema 校验、原子写、路径收容、临时工作区、资源闭包、operation-request → operation-result 管道、业务中立的事件日志与派生快照。明确排除：业务语义、任务编排、Git 写入、模型调用、远程网络、发布状态。见 `HARNESS_EXCLUSIONS`。

## 安装和最小示例

0.22.0 是本地源码候选。候选验证先把三个包分别打入同一个临时目录，再安装这三个精确 tarball：

```sh
pack_dir="$(mktemp -d)"
(cd packages/skill-family-contracts && pnpm pack --pack-destination "$pack_dir")
(cd packages/skill-family-harness-node && pnpm pack --pack-destination "$pack_dir")
(cd packages/skill-family-engineering-kit && pnpm pack --pack-destination "$pack_dir")
mkdir "$pack_dir/consumer" && (cd "$pack_dir/consumer" && npm init -y)
(cd "$pack_dir/consumer" && npm install "$pack_dir/skill-family-contracts-0.22.0.tgz" "$pack_dir/skill-family-harness-node-0.22.0.tgz" "$pack_dir/skill-family-engineering-kit-0.22.0.tgz")
```

发布后再使用 registry 坐标：

```sh
npm install skill-family-harness-node@0.22.0
npm info skill-family-harness-node --help
```

最小示例演示在 Node 内校验一份契约文档：

```js
// 发布后从已安装的消费者目录运行。
import { validateContractDocument } from "skill-family-harness-node";

const document = {
  schemaVersion: 1,
  kind: "skill-family.project-manifest",
  project: { id: "my-project", name: "My Project", description: "Example" },
  contracts: { version: "1.0.0", profile: "generic" },
  managedFiles: ["package.json"],
  updatedAt: "2026-01-01T00:00:00Z",
};

const result = validateContractDocument(document, {
  schemaId: "https://contracts.skill-family.example/v1/project-manifest.json",
});
if (!result.valid) console.error(result.errorCode);
```

以上代码展示了 `validateContractDocument` 的基本调用；它复用 Contracts 的校验器并以 schema 为键缓存实例，不重新编译。

## Candidate Quickstart Profile

需要构造带 observation 绑定的 Task、封装终态 Result，并复验两个文档是否绑定精确 observation 字节与 correlation 字段时，使用 candidate 子路径：

```js
import {
  createQuickstartTask,
  wrapQuickstartResult,
  verifyQuickstartExchange,
} from "skill-family-harness-node/quickstart-profile";
```

v2 机制会重算每个 path-backed output 和 evidence Resource 的真实字节摘要，并拒绝重复 Resource id、correlation 漂移、Task digest 变化，以及缺失或错配的 evidence binding。它不执行领域审计，不选择 method，不编排重试，也不拥有生命周期状态。

该能力仍是 **candidate**，评估时必须精确锁定三个 Foundation 包。0.10.0 新增上面的规范入口；历史 `/candidate/quickstart-profile` 入口作为同源迁移别名继续可用。消费者迁移一次后，未来晋升 stable 不再切入口或重建相同 Bundle。仍生产 candidate v1 交换的接入必须继续精确锁定 `0.2.1`。

## 典型使用场景

- 需要在 Node 内安全地读/写受收容路径：用 path containment 与 atomic write。
- 需要把资源归一成可复算闭包或生成摘要：用 resource closure。
- 需要从机器结果生成人类报告：用 report model/render/binding/check。
- 需要持久化事件日志与派生快照：用 state-store（事件含义由调用方拥有）。
- 需要在分散路径上应用一组有序普通文件，并支持重启恢复与显式清理：用文件集合 apply/recovery 入口。

## 边界

- 消费 `skill-family-contracts`，复用其方言路由的 Ajv validator、Kernel Protocol、冻结错误码与 fixture；不复制协议定义，不重新解释 Schema。
- 只实现机制：Schema 校验、原子写、路径收容、临时工作区、资源闭包、operation-request → operation-result 管道、业务中立的事件日志与派生快照。
- 明确排除：业务语义、任务编排、Git 写入、模型调用、远程网络、发布状态。见 `HARNESS_EXCLUSIONS`。

## 公共 API

| 导出 | 职责 |
| --- | --- |
| `HARNESS_CAPABILITIES` / `HARNESS_EXCLUSIONS` | 能力与排除清单（冻结常量）。 |
| `FOUNDATION_PACKAGE_VERSION` | 供锁步检查使用的精确 Foundation 包版本。 |
| `HarnessError` / `HARNESS_ERROR_KINDS` / `mechanismError` | 机制失败统一携带注册错误码 `SFC2004`，`details.kind` 给出稳定细分。 |
| `validateContractDocument` / `getValidator` / `resolveSchemaContext` / `validatorCacheSize` | 按 Schema 方言路由并缓存 validator；复用 Contracts 的 Ajv 实例与 dialect/policy 语义。 |
| `classifyPathInput` / `resolveContained` / `readFileContained` | 路径收容：拦截路径越界、符号链接逃逸、真实路径逃逸。 |
| `writeFileAtomic` | 原子写：失败不留半成品（临时文件 + fsync + rename）。 |
| `createAtomicWriteFake({ vector })` | 消费者契约测试用正式无文件系统测试替身；产生确定性的写入/替换/观察事实。 |
| `TemporaryWorkspace` / `createTemporaryWorkspace` / `withTemporaryWorkspace` | 自动清理的临时工作区，异常路径也清理。 |
| `digestBytes` / `computeResourceClosure` / `closureContains` | 资源闭包与确定性 sha256 摘要。 |
| `superviseProcess` / `validateTimeoutPolicy` | 唯一的受约束子进程监督器。0.11.0 的 `rawSink` 只向 fresh canonical 私有根写原始 stdout/stderr 字节，并等待子进程、流、排队写入、fsync 与句柄全部关闭。调用方必须在整个调用期间独占 sink 命名空间；句柄保护不证明 pathname 或根目录身份始终不变。 |
| `observeFilesystemTree` | 观察完整绑定树。默认/reject 保留既有 UTF-16 成员顺序；record 使用码点顺序，记录 symlink target bytes 且不跟随目标。 |
| `observeExecutableIdentity` | 观察调用方绑定的可执行入口、符号链接链、启动字节和脚本解释器链，供每次 spawn 前紧邻重观察。 |
| `createFixedSetPublicationManifest` / `publishFixedSet` / `replaceFixedSetAtomic` | 发布不替换的完整固定集合，或把完整暂存兄弟目录与一个既有目标原子交换。 |
| `parseRequest` / `processRequest` | 解析 `operation-request`，输出终态 `operation-result`。 |
| `validateReportModel` / `renderReportMarkdown` / `buildBinding` / `checkReport` | 消费经 Contracts 验证的 report model，确定性渲染中性 Markdown 并校验来源/结果/报告绑定；不解释业务输出。 |
| `normalizeAdapterSource` / `buildAdapterClosure` / `verifyAdapterBuildManifest` / `materializeAdapterBuild` | 通用文本 source closure、manifest 全摘要复验和目标集合原子落盘；具体 Profile/driver 不在 Harness。 |
| `probeVersionVector` | 默认禁用 spawn 的版本探测机制；显式启用时只执行绝对、无 symlink 的受审计向量，不使用 PATH/shell。 |
| `openStateStore` / `appendEvent` / `readEvents` / `verifyStateStore` / `closeStateStore` | 严格单写者的 append-only 事件存储；事件目录是唯一状态权威，`chain-head.json` 只是缓存。 |
| `readSnapshot` / `writeSnapshot` / `rebuildSnapshot` | 原子派生快照与完整事件重建；坏事件不能被旧快照掩盖，坏快照可被重建忽略。 |
| `inspectStateStoreLock` / `recoverStateStoreLock` | 只读锁诊断与显式恢复；恢复有**两种互斥接管模式**——旧模式精确匹配观测到的 owner + fencing，维护模式要求该 root 的完整观察加两项显式确认；混用两模式字段（含显式写成 `undefined` 的旧模式字段）被拒绝。 |
| `applyFileSet` / `recoverFileSet` / `pruneFileSetRecovery` | 在绑定根下对分散普通文件执行一组有序 create/replace/delete，通过显式公共入口重启恢复未提交操作，并精确清理已终结操作的材料。前提是合作式排他，只给出逐路径意图事实，不承诺瞬时多文件可见性。 |

## 替换既有固定集合

`replaceFixedSetAtomic({ sourceRoot, targetParent, targetSegment })` 要求 `sourceRoot` 与既有目标是同一规范父目录下的真实兄弟目录。它先复验两棵完整目录树，再执行一次 Darwin `RENAME_SWAP` 或 Linux `RENAME_EXCHANGE`。确认成功后，新集合位于目标路径，被置换的旧目标留在 `sourceRoot`；Foundation 不删除任一目录。

该操作不是幂等操作：相同路径再次调用会把两个目录交换回去。确认成功后，或错误状态为提交后及不确定时，不得盲目重试。机制失败使用 `SFC2004` 与 `details.kind: "atomic-replace-failed"`，并给出提交阶段、发布、提交、验证和持久化状态，供调用方自行恢复。

## 状态存储的锁与恢复边界

- 锁使用 exclusive create，第二写者立即收到 `store-locked`；不排队，也不按时间、PID 或租约过期偷锁。
- `inspectStateStoreLock` 不创建任何文件。默认返回 `owner`、单调 `fencing`、`ageMs` 和恢复中标记，`ageMs` 仅供诊断、从不参与正确性判断；只有显式传入 `{ recoveryObservation: true }` 时才改为返回完整的 `state-store-recovery-observation` 观察对象（维护模式需要它），默认诊断结果不是有效观察。
- 崩溃遗留锁只能由调用方在 Foundation 之外确认旧写者已经终止后接管。`recoverStateStoreLock` 有两种**互斥**模式，只能取其一；两种模式都必须提供 `payloadSchemas`：与正常打开相同的 eventType→版本→JSON Schema 注册表，至少一个条目。恢复返回的新写者句柄用它校验后续事件负载。`newOwner` 与 `clock` 可选：
  - 旧模式：`recoverStateStoreLock(root, { expectedOwner, expectedFencing, confirmOwnerTerminated: true, payloadSchemas })`，owner 与 fencing 必须精确匹配当前观测值；不匹配、缺少确认或缺少 `payloadSchemas` 均失败关闭。
  - 维护模式：`recoverStateStoreLock(root, { observation, confirmAllParticipantsStopped: true, confirmExclusiveMaintenance: true, payloadSchemas })`。`observation` 必须是对同一 root 调用 `inspectStateStoreLock(root, { recoveryObservation: true })` 取得的完整观察；它覆盖 writer 缺失与部分写入的控制文件，不只核对 owner/fencing。
  两种模式的字段不得混用：显式写出但值为 `undefined` 的旧模式字段同样计入混用并被拒绝，构造维护模式 options 时必须整体省略旧模式字段。
- 维护模式的两项确认是外部信任前提，不是布尔字段自动实现的锁：`confirmAllParticipantsStopped` 声明旧写者、旧恢复者及其子进程均已停止，`confirmExclusiveMaintenance` 声明排他维护区间仍然成立。维护区间从取得观察**之前**开始，到本次调用取得新写者句柄或失败返回结束；区间内禁止其他恢复、正常打开、状态存储写入与相关业务写入。PID、年龄或 owner/fencing 比较都不能替代该前提；取得句柄后由正常写者合同继续保护。

```js
// 维护接管：先在外部维护区间内取得完整观察，再只提交维护模式字段。
const observation = await inspectStateStoreLock(stateStoreRoot, { recoveryObservation: true });
const store = await recoverStateStoreLock(stateStoreRoot, {
  observation,
  confirmAllParticipantsStopped: true,
  confirmExclusiveMaintenance: true,
  payloadSchemas, // 必填：恢复返回的新写者句柄用它校验后续事件负载
});
```
- 恢复产生更大的 fencing。旧 handle 每次 append 都重新核对 owner、fencing 和 acquisition id；事件最终发布使用同目录临时普通文件、fsync 和 exclusive link，绝不覆盖既有 sequence。
- append、snapshot、close 与 recovery 由短期 `writer-mutation.lock` 串行化；恢复不能越过已经持有 mutation guard 的权威写入。
- 如果恢复进程自身在持有 `writer-recovery.lock` 时崩溃，系统保持可诊断的锁死状态，普通路径不自动删除该 guard：只有重新建立排他维护区间、重新观察后经维护模式，才整理可处置的事件临时别名与 `writer.lock`、`writer-mutation.lock`、`writer-recovery.lock` 三种控制残留。未知格式的控制记录或 fencing 计数器拒绝接管，不猜测、不归零，未知文件不删除。当前 API 不声称解决不可信调用方谎报“旧写者已终止”的场景。
- state root、`events/`、`snapshots/`、事件和快照拒绝 symlink、硬链接、FIFO、设备与其它非普通条目。payload 必须是纯 JSON，且 `eventType + payloadSchemaVersion` 必须命中调用方在 open/recover 时冻结的 Schema 对。

## 多路径文件集合的应用与恢复边界

- `applyFileSet(request, { validate })` 在绑定根下对分散普通文件执行一组有序 create/replace/delete；`validate` 是调用方提供的只读函数。恢复与清理走公共入口 `recoverFileSet(request)` 与 `pruneFileSetRecovery(request)`，不依赖私有路径。
- 恢复材料位于根内固定 `.foundation-file-apply/`（`journal/` 与 `operations/<id>/{before,after}/<index>`），默认保留；只有显式 prune 才会清理某个已终结操作的精确材料并保留 journal。未终结与冲突操作的材料不清理，未知邻接暂存文件不按后缀删除，只报告 `possible-unknown`。
- 整组前检（整体拒绝、环境不支持、容量越限）在任何业务写入前返回，业务零写入；未为某路径写入持久 `apply-intent` 事实前不触碰该业务路径。
- 领域验证返回 false、抛错、超时或返回非法结果会触发自动恢复，验证状态与恢复状态分开报告。有效提交事件决定不再回滚；目标复核或持久化仍未确认时结果报告 `commit-unconfirmed` 并保留材料。
- 锁只是合作式排他：恢复要求调用方先在 Foundation 之外建立排他维护区间，并提交该区间的完整 `maintenance` 观察（对根内固定 `.foundation-file-apply/journal` 调用 `inspectStateStoreLock(journalRoot, { recoveryObservation: true })` 取得，`observation.root` 必须是该 journal 目录；默认诊断结果不能用于故障接管）；清理在无残留时直接取得正常 writer，只在需要故障接管时才要求同一 `maintenance` 观察。不承诺瞬时多文件可见性，也不抵御不合作的并发写者。
- 可捕获失败返回 `file-set-result`；外码沿用 `SFC2004`，`details.kind` 与 `details.phase` 为闭枚举，结果中的 `outcome`、`paths`、`materials` 给出逐路径事实。锁释放失败通过 `errors` 报告而不是抛出。

## 稳定错误码

全部复用 Contracts 冻结登记表，不新增未登记码。机制失败统一为 `SFC2004`（EXECUTION_FAILED），`details.kind` 取 `HARNESS_ERROR_KINDS` 中的稳定值，例如 `path-traversal`、`symlink-escape`、`realpath-escape`、`atomic-write-failed`、`missing-resource`、`workspace-disposed`。

新增一个全新的 SFC 码属于 Contracts 变更（登记表在 contracts 包内），超出本包写集；因此用「`SFC2004` + 稳定 `details.kind`」组合保持对外语义稳定。

## 路径收容模型

`resolveContained(root, rel)` 是所有文件系统访问的唯一入口，按序拒绝：

1. 输入分级（`classifyPathInput`，纯函数可测）：拒绝绝对路径、Windows 盘符/UNC 路径、POSIX 上的反斜杠路径、空输入、NUL 字节。
2. 词法收容：`path.resolve` 后离开根 → `path-traversal`。
3. 符号链接逃逸：末位组件是指向根外的符号链接（或断链）→ `symlink-escape`。
4. 真实路径逃逸：任一中间符号链接链的规范化结果离开根 → `realpath-escape`。

比较是基于 `realpath` 之后的规范根，避免 macOS `/var → /private/var` 一类系统级符号链接造成误判。

## 测试

`node --test` 覆盖：Contracts fixture 全量回放、安全反例、原子性失败路径、临时工作区、闭包确定性、报告事实绑定与 Markdown 注入、宿主 manifest/路径/命令信任，以及状态存储的崩溃、并发、损坏、fencing、显式恢复、symlink、硬链接与 FIFO 反例。文件集合 apply/recovery 另覆盖崩溃重启恢复、验证失败自动恢复、prune 与冲突保留反例。

## 故障诊断

机制失败统一抛出 `SFC2004`（EXECUTION_FAILED），`details.kind` 给出稳定细分（如 `path-traversal`、`atomic-write-failed`）。如失败，检查 root 路径是否正确且目标文件未被锁定。

## 深入文档入口

- 架构边界与路由：[架构说明](https://ifoohoo.github.io/skill-family-engineering-kit/architecture/)、[智能体架构路由](https://ifoohoo.github.io/skill-family-engineering-kit/agents/architecture-routing/)
- 能力目录：[capability-catalog.json](https://ifoohoo.github.io/skill-family-engineering-kit/agents/capability-catalog.json)
- 副作用矩阵：[失败与副作用矩阵](https://ifoohoo.github.io/skill-family-engineering-kit/reference/failure-and-side-effect-matrix/)

<!-- agent-quick-reference:start -->
## Agent Quick Reference

### Use when

- 需要在 Node 内校验契约、安全读写受收容路径、计算资源闭包、渲染确定性报告。
- 需要持久化事件日志与派生快照，或归一化宿主适配源。
- 需要试用非稳定 Quickstart 交换，并复验 observation/task/result 绑定。

### Do not use when

- 需要把文件选择的业务规则放入 Foundation（业务规则由调用方拥有）。
- 需要宿主身份策略、宿主专属生命周期计划、远端发布、删除式 uninstall 或二进制 adapter source。宿主策略与生命周期计划归 Engineering Kit。
- 需要领域审计语义、重试编排或兼容性已冻结的 Quickstart API。

### Capability selection

- `foundation.harness.contract-validation`：Node 内契约校验与校验器缓存。
- `foundation.harness.path-containment`：路径分类与受收容解析，拒绝三类逃逸。
- `foundation.harness.atomic-write`：受收容路径内原子写，失败回滚。
- `foundation.harness.temporary-workspace`：自动清理的临时工作区。
- `foundation.harness.resource-closure`：确定性资源闭包与 sha256 摘要。
- `foundation.harness.supervise-process`：监督一个有界子进程；原始证据收集仍只提供机制，不产生收据或领域结论。
- `foundation.harness.request-processing`：operation-request → 终态 operation-result。
- `foundation.harness.report`：report-model 校验/渲染/绑定/检查。
- `foundation.harness.host-adapter`：adapter source closure/build/materialize 与版本探测。
- `foundation.harness.state-store`：append-only 事件、hash chain、快照与锁恢复。
- `foundation.harness.file-set-recovery`：在分散普通文件上执行有序多路径 create/replace/delete，重启恢复未提交操作，并精确清理已终结材料。
- `foundation.harness.errors`：机制错误类型与稳定错误类。
- `foundation.harness.quickstart-profile-candidate`：锁定精确版本后构造 observation/task/result 并复验绑定。

### Required inputs

- 受收容根目录（路径收容的边界）。
- 待校验/待写入的文档、资源或事件负载。
- 多路径文件集合：绑定同一根与 environment 的有序操作组，apply 另需调用方提供的只读 `validate` 函数；recover 或故障接管式 prune 另需完整的 `maintenance` 观察。

### Outputs and evidence

- 校验结果、受收容绝对路径、原子写文件、闭包摘要、终态结果、报告文本、事件/快照。
- 证据：`packages/skill-family-harness-node/test/validation.test.mjs`、`atomic.test.mjs`、`containment.test.mjs`、`closure.test.mjs`、`report.test.mjs`、`state-store.test.mjs`、`file-set-recovery.test.mjs`、`file-set-recovery-crash.test.mjs`。

### Side effects

- 只读文件系统访问（path/atomic/workspace/state-store 在受收容路径内读写）。
- `HARNESS_EXCLUSIONS` 明确排除 release-state、remote-network-access、business-semantics、workflow-orchestration、model-calls、git-writes。

### Failure semantics

- 机制失败统一 `SFC2004`，`details.kind` 为稳定细分（如 `path-traversal`、`atomic-write-failed`）。
- 失败后残余状态：原子写回滚临时文件；状态存储链断裂抛错，旧快照可被重建忽略。
- 文件集合失败后的残余状态：恢复材料留在根内固定 `.foundation-file-apply` 下；验证失败触发自动恢复，验证状态与恢复状态分开报告；提交未确认时报告 `commit-unconfirmed` 并保留材料。

### Architectural invariants

- Event meaning and reducer transitions remain consumer-owned；state-store 只提供底座。
- 文件集合协议不新增第二套事件日志、锁、序列或摘要链：它复用既有发布、原子替换、绑定读取与摘要机制，同一绑定根在任一时刻只允许一个合作式写者，也从不清理状态存储的内部文件。
- 仅支持文本 adapter source（utf8），不支持二进制投影。

### Route elsewhere when

- 业务状态机/终态：转 loop-agent。
- 宿主身份策略、宿主 driver 和生命周期授权归 Engineering Kit；Harness 只提供可复用的绑定读取、严格发布、原子写、闭包和探测机制。
- 领域审计语义：转独立审计消费者。

### Machine-readable sources

- 公开能力目录：[`capability-catalog.json`](https://ifoohoo.github.io/skill-family-engineering-kit/agents/capability-catalog.json)（`foundation.harness.*` 条目）。
- 包内源：`src/*.mjs`。
- 包内 Candidate 源：`candidate/quickstart-profile.mjs`；规范公共导入：`skill-family-harness-node/quickstart-profile`；历史迁移别名：`skill-family-harness-node/candidate/quickstart-profile`。
<!-- agent-quick-reference:end -->

## 完整插件候选能力

新增候选 observeFilesystemTree({ root, rootBinding }) 读取完整树事实；既有 superviseProcess 支持可选每流原始字节上限。观察完成不等于接受载荷。

`observeFilesystemTree({ root, rootBinding, symlinkPolicy: { mode: "record" } })` 适用于宿主命令完成后、调用方已隔离且扫描期间没有并发 namespace writer 的稳定安装树、缓存树或投影树。它记录 symlink 自身的 `targetBase64`、`bytes` 和 `statMode`，只读取原始 target bytes，绝不跟随 target；普通文件仍复用 `readFileBound`。record 是 Node/JS best-effort candidate：发现扫描期间的漂移会失败关闭，但不提供事务快照、同 UID 恶意并发或 ABA 保证，这不改变稳定树结果的正常使用语义。release-skill 等消费者负责调用时机，并继续负责 npm `.bin` 等领域接受规则。

成员排序按模式区分，以保留兼容性：省略策略或使用 reject 时沿用 0.14 的 UTF-16 关系顺序，record 使用 Unicode 码点顺序；`membersDigest` 也按对应顺序推导。

真实威胁包含恶意并发时，应形成最小上游能力缺口并交回 Foundation 裁决；不要在技能族内复制通用 walker、native addon、Harness、schema、Registry、runner 或 receipt 链，也不要自行升级为四平台原生实现。

另一个独立候选 `observeExecutableIdentity({ boundRoots, lookup, interpreterPolicy? })` 只对调用方显式提供的根和查找路径做逐次只读观察，供正式启动前紧邻重观察。`/usr/bin/env` shebang 通过显式 `pathEntries` 找到解释器时，结果保留解释器候选的完整 symlink chain，不折叠成最终文件。它不属于 `host-adapter`，也不证明 wrapper 控制流、ambient `PATH`、fd-exec/内核映像、签名信任、跨调用缓存、宿主支持/生命周期或领域接受；这些语义仍由调用方负责。候选入口存在不等于宿主已获资格。

0.22.0 是本地源码候选，远端可用性须由对应的 release-skill 发布后证据证明。候选检查使用本地已验证的三包 tarball；版本标记、单元测试或安装成功都不等于契约接入完成、迁移完成或真实宿主资格。

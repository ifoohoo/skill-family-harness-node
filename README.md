<!-- release-skill:safe-first-command -->
<!-- release-skill:external-write-boundary -->

# skill-family-harness-node

Contracts 机制协议的**唯一默认 Node 实现**。这是一个薄运行时（thin runtime）：只实现机制协议，不引入业务语义，不做第二语言实现。

## 边界

- 消费 `skill-family-contracts`（工作区依赖），复用其方言路由的 Ajv validator、Kernel Protocol、冻结错误码与 fixture；不复制协议定义，不重新解释 Schema。
- 只实现机制：Schema 校验、原子写、路径收容、临时工作区、资源闭包、operation-request → operation-result 管道。
- 明确排除：业务语义、任务编排、Git 写入、模型调用、远程网络、发布状态。见 `HARNESS_EXCLUSIONS`。

## 公共 API

| 导出 | 职责 |
| --- | --- |
| `HARNESS_CAPABILITIES` / `HARNESS_EXCLUSIONS` | 能力与排除清单（冻结常量）。 |
| `HarnessError` / `HARNESS_ERROR_KINDS` / `mechanismError` | 机制失败统一携带注册错误码 `SFC2004`，`details.kind` 给出稳定细分。 |
| `validateContractDocument` / `getValidator` / `resolveSchemaContext` / `validatorCacheSize` | 按 Schema 方言路由并缓存 validator；复用 Contracts 的 Ajv 实例与 dialect/policy 语义。 |
| `classifyPathInput` / `resolveContained` / `readFileContained` | 路径收容：拦截路径越界、符号链接逃逸、真实路径逃逸。 |
| `writeFileAtomic` | 原子写：失败不留半成品（临时文件 + fsync + rename）。 |
| `TemporaryWorkspace` / `createTemporaryWorkspace` / `withTemporaryWorkspace` | 自动清理的临时工作区，异常路径也清理。 |
| `digestBytes` / `computeResourceClosure` / `closureContains` | 资源闭包与确定性 sha256 摘要。 |
| `parseRequest` / `processRequest` | 解析 `operation-request`，输出终态 `operation-result`。 |

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

`node --test` 覆盖：Contracts fixture 全量回放、安全反例（三类逃逸均被拦截且零越界写）、原子性失败路径无残留、临时工作区异常清理、闭包确定性、Windows 路径/整数/正则/`format` 边界。

## 安装

```sh
npm install skill-family-harness-node@0.1.3
npm info skill-family-harness-node --help
```

## 最小示例

```js
// 从空目录运行：npm install skill-family-harness-node@0.1.3
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

## 故障诊断

机制失败统一抛出 `SFC2004`（EXECUTION_FAILED），`details.kind` 给出稳定细分（如 `path-traversal`、`atomic-write-failed`）。如失败，检查 root 路径是否正确且目标文件未被锁定。

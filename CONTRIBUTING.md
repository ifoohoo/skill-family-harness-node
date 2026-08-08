# 贡献指南（Contributing）

感谢关注 `skill-family-foundation-workspace`。本文件说明本地开发环境、
验证流程与 PR 规则。行为准则见 [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)，
Git 写操作的授权边界见仓库内文档站的生命周期指南（`docs/git-lifecycle.md`）。

拓扑说明：本工作区仓永久 private，是唯一开发真源；三个 public 仓
（`skill-family-contracts`、`skill-family-harness-node`、
`skill-family-engineering-kit`）是版本化发布镜像，不承载完整开发历史与
测试。本指南描述的是私有工作区内经授权的开发流程；消费坐标与发布状态
唯一见 `docs/status/current.md`。

## Bug 报告与 Issue

普通 bug 报告欢迎通过各 public 发布镜像仓的 GitHub Issues 提交（例如
`https://github.com/ifoohoo/skill-family-engineering-kit/issues`）。提交
bug 报告时请尽量提供：受影响包与版本、最小复现步骤、预期与实际行为。

**未经事先讨论的 Pull Request 不予接受。** 如果你希望贡献代码，请先
通过 Issue 讨论变更的动机与范围，获得维护者确认后再提交 PR。这是为了
避免重复劳动、确保变更与本项目的架构裁决和发布合同一致。

## 本地安装

必需工具（精确版本见仓库内 `docs/setup.md` 的安装合同）：

- Node.js：`>=22.22.2 <23`（版本取自 `.node-version`）
- pnpm：10.30.0（`packageManager` 精确锁定）
- Python：3.10 或更高（仅文档站构建需要）

```bash
pnpm install
pnpm synth
pnpm check
```

- `pnpm synth` 再生成 projen 受管文件。受管文件只能通过修改
  `.projenrc.js` 间接变更；手写源码、文档和 fixture 不会被 synth 覆盖。
- `pnpm check` 是统一根门禁：10 个稳定门禁 ID 按固定顺序串联执行，
  任一步骤非 0 即整体失败。

## 提交 PR 前必须做到

1. 只修改当前任务授权的文件，不顺手改动无关文件；
2. 修改 `.projenrc.js` 后运行 `pnpm synth`，并保证连续两次 synth 零漂移；
3. 本地完整通过根门禁：

   ```bash
   pnpm check
   ```

4. 涉及文档时额外运行文档事实与链接检查：

   ```bash
   node scripts/docs/fact-check.mjs
   node scripts/docs/link-check.mjs
   ```

5. 涉及三个叶子包时，确认发布字节合同仍然成立：

   ```bash
   pnpm run release-artifacts:build -- --output .artifacts/publish-candidates
   pnpm run release-artifacts:verify -- --root .artifacts/publish-candidates --manifest .artifacts/publish-candidates/release-artifacts.manifest.json
   ```

## PR 规则

- PR 描述须包含：动机、变更面、本地门禁结果（退出码）与剩余风险；
- 不得为转绿而弱化任何产品门禁、Action pin、权限、泄漏策略或 tarball 合同；
- 不新增运行时依赖，除非先经评审并显式修订泄漏策略中的公开依赖清单；
- 不提交生成物与本地状态：`node_modules`、`site/`、`.artifacts/`、缓存、
  日志、凭据或本机绝对路径一律不进入提交；
- 受管投影必须与产生它的源变更在同一 PR 内提交，禁止手改受管文件；
- 动态状态（实时发布状态、远端对象状态）不写入版本化文档，唯一产品状态
  口径见 `docs/status/current.md`。

## 安全报告

漏洞报告渠道与支持版本见 [SECURITY.md](SECURITY.md)。

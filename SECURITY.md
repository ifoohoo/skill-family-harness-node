# 安全策略（Security Policy）

## 支持的版本

| 版本 | 安全支持状态 |
| --- | --- |
| 0.1.x | 当前维护线：接受安全报告 |
| 0.0.0 | 未发布的内部骨架版本，不在公开支持范围 |

安全修复只承诺在最新的 0.1.x 线上提供；更早版本不回移修复。

## 报告漏洞

请勿通过公开 issue 直接披露未修复的安全漏洞。private workspace 仓永久
private，不是公开报告渠道；请通过以下渠道私下报告：

1. **首选**：通过任一 public 发布镜像仓（例如
   `https://github.com/ifoohoo/skill-family-engineering-kit`）在 GitHub 上使用
   **Private vulnerability reporting**（仓库 Security 页签的
   "Report a vulnerability"）；
2. **备选**：如上述通道不可用，请通过邮件联系仓库维护者
   （`ifoohoo` GitHub 组织的 `security` 团队，或仓库 `README.md` 中列出的
   维护者邮箱）。

报告时请尽量提供：

- 受影响的包与版本（`skill-family-contracts`、`skill-family-harness-node`、
  `skill-family-engineering-kit`）；
- 可复现的最小步骤或概念验证；
- 影响范围与利用前提。

## 处置承诺

- 收到报告后先确认受理，再按影响面排期修复；
- 修复发布前不公开披露细节；
- 修复随下一个补丁版本发布，并在该版本的变更记录中说明。

本文件只描述漏洞报告渠道与支持版本；实时发布状态不属于本文件范围。

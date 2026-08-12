# 变更日志

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

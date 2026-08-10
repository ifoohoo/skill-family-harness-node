# 变更日志

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

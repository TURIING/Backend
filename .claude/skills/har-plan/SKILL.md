---
name: har-plan
description: 创建变更规划 — 按 proposal → specs → design → tasks 顺序生成所有规划制品。用于把想法转化为可执行的规划。
---

创建变更规划，生成所有制品。

---

**输入**: 变更名称（kebab-case）和变更描述。

**步骤**

1. **如果输入为空，询问用户想做什么**

   如果未提供变更名称或描述，问用户想构建什么。从描述中推导 kebab-case 名称。

2. **创建变更目录**

   ```bash
   mkdir -p .claude/har/changes/<name>/specs
   ```

   如果目录已存在，询问用户是继续还是新建。

3. **按依赖顺序生成制品**

   按 proposal → specs → design → tasks 的顺序生成。每个制品生成前先读取其依赖的已完成制品。

   ### a. proposal.md

   模板：
   ```markdown
   ## Why
   <!-- 1-2句话说明动机 -->

   ## What Changes
   <!-- 变更内容列表。标注 BREAKING -->

   ## Capabilities

   ### New Capabilities
   - `<name>`: <描述>

   ### Modified Capabilities
   - `<existing-name>`: <变更描述>

   ## Impact
   <!-- 受影响代码、API、依赖 -->
   ```

   ### b. specs/<capability>/spec.md（delta specs）

   为每个 capability 创建 spec 文件。

   格式：
   ```markdown
   ## ADDED Requirements

   ### Requirement: <需求名>
   <描述。使用 SHALL/MUST>

   #### Scenario: <场景名>
   - **WHEN** <条件>
   - **THEN** <预期结果>

   ## MODIFIED Requirements

   ### Requirement: <已有需求名>
   <!-- 完整需求块 -->

   ## REMOVED Requirements

   ### Requirement: <被删除需求>
   **Reason**: <原因>
   **Migration**: <迁移方案>
   ```

   规则：
   - 每个需求至少有一个 `#### Scenario:`
   - 场景使用 4 个 `#`
   - MODIFIED 必须包含完整需求块（不能只写增量）

   ### c. design.md

   模板：
   ```markdown
   ## Context
   <!-- 背景和当前状态 -->

   ## Goals / Non-Goals
   **Goals:**
   **Non-Goals:**

   ## Decisions
   <!-- 关键技术决策及理由。包含替代方案 -->

   ## Risks / Trade-offs
   <!-- [风险] → 缓解措施 -->

   ## Open Questions
   <!-- 待解决问题 -->
   ```

   ### d. tasks.md

   模板：
   ```markdown
   ## 1. <任务组>

   - [ ] 1.1 <任务描述>
   - [ ] 1.2 <任务描述>
   ```

   指南：
   - 按依赖排序
   - 任务小而可验证
   - checkbox 格式：`- [ ] X.Y <描述>`

4. **显示完成总结**

   所有制品完成后，总结变更名称、位置、制品列表，提示运行 `/har:apply`。

**护栏**
- 按依赖顺序生成
- 每个制品前先读已完成制品
- 上下文严重不足时询问用户
- 变更名冲突时询问用户
- 每个制品写入后验证文件存在
- proposal 的 Capabilities 列表决定 specs 的数量和名称

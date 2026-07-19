---
name: "HAR: 规划"
description: "创建变更规划 — 生成 proposal、specs、design、tasks 所有制品"
category: Workflow
tags: [workflow, plan, artifacts]
---

创建变更规划，一键生成所有制品。

我会创建以下制品：
- proposal.md（为什么做、做什么）
- specs/（需求规格 - delta specs）
- design.md（怎么做）
- tasks.md（实现步骤清单）

准备实现时，运行 `/har:apply`

---

**输入**: `/har:plan` 后面跟变更名称（kebab-case），或用户想构建什么的描述。

**步骤**

1. **如果输入为空，询问用户想做什么**

   如果用户未提供任何输入，问："你想做什么变更？描述你想构建或修复的内容。"

   从描述中推导 kebab-case 名称（例如："添加用户认证" → `add-user-auth`）。

   **重要**: 不理解用户意图前不要继续。

2. **创建变更目录**

   在 `.claude/har/changes/` 下创建变更目录：
   ```bash
   mkdir -p .claude/har/changes/<name>/specs
   ```

   如果该目录已存在，询问用户是继续现有变更还是创建新的。

3. **按依赖顺序生成制品**

   按以下顺序依次生成每个制品，每个制品生成前先读取其依赖的已完成制品：

   ### a. proposal.md — 为什么做

   模板：
   ```markdown
   ## Why
   <!-- 1-2句话说明动机。解决什么问题？为什么现在要做？ -->

   ## What Changes
   <!-- 列出变更内容。标注 BREAKING 变化 -->

   ## Capabilities

   ### New Capabilities
   <!-- 新能力域，每个是一个 specs/<name>/spec.md -->
   - `<name>`: <简要描述>

   ### Modified Capabilities
   <!-- 需求层面有变化的已有能力域 -->

   ## Impact
   <!-- 受影响的代码、API、依赖、系统 -->
   ```

   ### b. specs/ — 需求规格（delta specs）

   为 proposal 中列出的每个 capability 创建一个 `specs/<capability>/spec.md`。

   格式：
   ```markdown
   ## ADDED Requirements

   ### Requirement: <需求名称>
   <描述。使用 SHALL/MUST 表示规范性需求>

   #### Scenario: <场景名>
   - **WHEN** <条件>
   - **THEN** <预期结果>

   ## MODIFIED Requirements
   <!-- 仅当修改已有 spec 时使用 -->

   ### Requirement: <已有需求名>
   <!-- 完整的需求块（含所有场景） -->

   ## REMOVED Requirements
   <!-- 仅当删除需求时使用 -->

   ### Requirement: <被删除的需求>
   **Reason**: <删除原因>
   **Migration**: <迁移方案>
   ```

   关键规则：
   - 每个需求至少有一个场景（`#### Scenario:`）
   - 场景使用 4 个 `#`
   - 新增能力用 `## ADDED Requirements`
   - 修改已有能力用 `## MODIFIED Requirements`（必须包含完整需求块）

   ### c. design.md — 怎么做

   模板：
   ```markdown
   ## Context
   <!-- 背景、当前状态、约束 -->

   ## Goals / Non-Goals
   **Goals:** <!-- 本次设计目标 -->
   **Non-Goals:** <!-- 明确不在范围内 -->

   ## Decisions
   <!-- 关键技术决策及理由。为什么选 X 而非 Y？包含考虑过的替代方案 -->

   ## Risks / Trade-offs
   <!-- 已知风险和权衡。格式: [风险] → 缓解措施 -->

   ## Open Questions
   <!-- 待解决的设计问题 -->
   ```

   ### d. tasks.md — 实现步骤

   模板：
   ```markdown
   ## 1. <任务组名称>

   - [ ] 1.1 <任务描述>
   - [ ] 1.2 <任务描述>

   ## 2. <任务组名称>

   - [ ] 2.1 <任务描述>
   ```

   指南：
   - 按依赖关系排序任务
   - 每个任务足够小（每次会话可完成）
   - 每个任务必须用 checkbox 格式：`- [ ] X.Y <描述>`

4. **完成总结**

   所有制品生成完毕后，显示：
   - 变更名称和位置
   - 创建的制品列表
   - "所有制品已创建！运行 `/har:apply` 开始实现。"

**护栏**
- 按依赖顺序生成，每个制品前先读已完成的
- 如果上下文严重不足，询问用户 — 但倾向于做出合理决策保持推进
- 如果变更名称已存在，询问继续还是新建
- 每个制品写入后验证文件存在再继续

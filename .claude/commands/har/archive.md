---
name: "HAR: 归档"
description: "归档已完成的变更 — 同步 specs、提取知识到 rules/memory、归档变更目录"
category: Workflow
tags: [workflow, archive, knowledge]
---

归档已完成的变更。这个阶段做三件事：
1. 将 delta specs 合并到主 spec 库
2. 从变更中提取有价值的项目知识（先展示，确认后写入 rules/ 或 memory/）
3. 将变更目录移到 archive/

---

**输入**: 可选指定变更名称（如 `/har:archive add-oauth`）。如省略，从对话上下文推断或列出可用变更。

**步骤**

1. **选择变更**

   如果未提供名称，列出 `.claude/har/changes/`（排除 archive/）中的变更让用户选择。只显示活跃变更。

   **重要**: 不自动选择，始终让用户选择。

2. **检查制品和任务完成状态**

   读取变更目录下的制品文件：
   - 检查 tasks.md 中 `- [ ]` 和 `- [x]` 的数量
   - 检查 proposal.md、design.md、specs/ 是否存在

   **警告展示**:
   - 有未完成制品 → 警告并请求确认
   - 有未完成任务 → 显示数量，警告并请求确认
   - 无 tasks.md → 跳过任务检查

3. **同步 delta specs 到主 spec 库**

   读取变更目录下 specs/ 中的每个 delta spec 文件。

   每个 delta spec 包含以下章节：
   - `## ADDED Requirements` — 新增需求
   - `## MODIFIED Requirements` — 修改已有需求
   - `## REMOVED Requirements` — 删除需求
   - `## RENAMED Requirements` — 重命名需求

   对每个 delta spec，智能合并到 `.claude/har/specs/<capability>/spec.md`：

   - **ADDED**: 追加到主 spec 的 Requirements 章节
   - **MODIFIED**: 在主 spec 中找到对应需求，替换为完整内容
   - **REMOVED**: 从主 spec 中删除整个需求块
   - **RENAMED**: 找到 FROM 需求，重命名为 TO

   如果主 spec 不存在（新能力域），创建 `.claude/har/specs/<capability>/spec.md`：
   ```markdown
   ## Purpose
   <!-- TBD -->

   ## Requirements
   <!-- 来自 delta spec 的 ADDED Requirements -->
   ```

   **智能合并原则**:
   - 对于 MODIFIED，只修改提到的部分，保留未提及的场景
   - 对于 ADDED，如果需求已存在则当作隐式 MODIFIED
   - 操作应该是幂等的 — 运行两次结果相同

4. **提取项目知识**

   反思本次变更的实施过程，识别以下类别的知识：

   | 知识类型 | 判断标准 | 写入位置 |
   |---------|---------|---------|
   | 编码约定/模式 | 反复出现的模式、项目特有的规范 | `.claude/rules/<topic>.md` |
   | 架构决策 | 选择 X 而非 Y 的原因、系统边界 | `.claude/rules/architecture.md` |
   | 模块关系 | 模块间依赖、调用链路、职责 | `memory/` |
   | 踩坑记录 | 配置注意事项、构建问题、边界条件 | `memory/` |
   | 设计方案 | 可复用的设计模式、通用解决方案 | `.claude/rules/` |

   **重要**: 只提取项目中**不显而易见**的知识。代码结构、git 历史等不需要提取。

5. **展示提取结果并请求确认**

   以结构化列表形式展示提取的知识：
   
   ```
   ## 知识提取结果

   ### 编码约定
   1. **[标题]** — 描述 → 写入 `.claude/rules/<file>.md`
   
   ### 架构决策
   1. **[标题]** — 描述 → 写入 `.claude/rules/architecture.md`
   
   ### 模块关系
   1. **[标题]** — 描述 → 写入 `memory/`
   
   ### 踩坑记录
   1. **[标题]** — 描述 → 写入 `memory/`
   ```

   用 AskUserQuestion 让用户逐条确认或拒绝。用户确认后才写入。

6. **写入知识**

   - **rules/**: 创建或更新 markdown 文件。如果是新主题，创建新文件。如果是对已有主题的补充，追加内容。
   - **memory/**: 按 Claude Code memory 格式（frontmatter + 内容）创建记忆文件，并更新 MEMORY.md 索引。

7. **归档变更目录**

   创建目标路径：
   ```
   .claude/har/changes/archive/YYYY-MM-DD-<name>/
   ```

   如果目标已存在，报错并建议处理方式。

   移动变更目录：
   ```bash
   mv .claude/har/changes/<name> .claude/har/changes/archive/YYYY-MM-DD-<name>/
   ```

8. **显示归档摘要**

   ```
   ## 归档完成

   **变更:** <name>
   **归档到:** .claude/har/changes/archive/YYYY-MM-DD-<name>/
   **Specs:** ✓ 已同步 / 无 delta specs / 跳过
   **知识提取:** 已写入 N 条 (rules: X, memory: Y)

   所有制品完整。所有任务完成。
   ```

**护栏**
- 始终让用户选择变更
- 归档前检查完成状态，有未完成项时警告但允许继续
- 知识提取先展示再写入，不自动写入
- 保留 `.openspec.yaml` 不处理（与 HAR 无关）
- 显示清晰的归档摘要
- 如果 delta specs 存在，始终进行同步评估

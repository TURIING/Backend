---
name: har-archive
description: 归档已完成的变更 — 同步 specs、提取知识到 rules/memory、归档变更目录。
---

归档已完成的变更：同步 specs → 提取知识 → 归档。

---

**输入**: 变更名称（可选）。

**步骤**

1. **选择变更**

   列出 `.claude/har/changes/`（排除 archive/）中的变更让用户选择。不自动选择。

2. **检查完成状态**

   解析 tasks.md 的 checkbox，检查制品是否存在。
   - 有未完成项 → 警告并确认
   - 全部完成 → 继续

3. **同步 delta specs**

   读取变更目录下 specs/ 中的每个 delta spec，智能合并到 `.claude/har/specs/<capability>/spec.md`：

   - **ADDED**: 追加到主 spec
   - **MODIFIED**: 原地替换完整需求块
   - **REMOVED**: 删除需求块
   - **RENAMED**: 重命名

   新能力域创建 `spec.md` 包含 Purpose（可 TBD）+ Requirements。

   合并原则：
   - MODIFIED 保留未提及的场景
   - 已存在需求用 ADDED 时当作 MODIFIED
   - 操作应幂等

4. **提取项目知识**

   反思变更过程，识别以下知识：

   | 类型 | 写入位置 |
   |------|---------|
   | 编码约定/模式 | `.claude/rules/<topic>.md` |
   | 架构决策 | `.claude/rules/architecture.md` |
   | 模块关系 | `memory/` |
   | 踩坑记录 | `memory/` |

   只提取不显而易见的知识。

5. **展示并确认**

   展示提取的知识列表，逐条让用户确认。不自动写入。

6. **写入知识**

   用户确认后：
   - rules/ 文件：创建或更新 markdown
   - memory/：创建带 frontmatter 的记忆文件，更新 MEMORY.md 索引

7. **归档**

   ```bash
   mv .claude/har/changes/<name> .claude/har/changes/archive/YYYY-MM-DD-<name>/
   ```

   目标已存在时报错。

8. **显示摘要**

   展示归档信息、spec 同步状态、知识提取数量。

**护栏**
- 用户选择变更
- 归档前检查并警告
- 知识提取先展示再写入
- 显示清晰摘要

# Change: port-vulkan-resource

## Why

VulkanDriver 需要句柄资源之上的生命周期管理：对象经 HandleAllocator 池化分配后，引用计数可能在任意线程归零，但 Vulkan 资源的销毁必须延迟到 backend 线程（本项目为双线程命令流架构，见 command-buffer-queue）。本项目已有句柄分配器（handle-allocator 能力域），但缺少其上的资源层：统一的创建/获取/销毁入口、跨线程引用计数与延迟销毁（gc）、以及语义级 use-after-free 检测。从 Filament `backend/src/vulkan/memory/` 移植 Resource 与 ResourceManager 填补这一层。

## What Changes

- 移植 `Resource` 结构到 `src/vulkan/resource/`：继承 `utils::Ref`，以 Ref 的原子计数取代 Filament 自管 `mCount` 位域；override `OnLastRef()` 注册延迟销毁而非 `delete this`
- 移植 `ResourceManager`：句柄分配/构造/获取/销毁统一入口 + GC 列表 + `gc()`/`terminate()`
- 扩展 `3rd/Utils` 的 `Ref`：`SubRef()` 归零时虚分发 protected `OnLastRef()`（默认实现仍是 `delete this`，向后兼容）
- 不移植 `resource_ptr`：以 `Utils::SharedPtr`（零改动）替代
- 砍掉 `ThreadSafeResource` 与 `isThreadSafeType` 分裂（Ref 计数本身原子）；GC 列表合并为单列表 + 锁
- 类型表最小骨架：`ResourceType` 枚举 22+1 完整移植、`getTypeStr` 完整；`getTypeEnum` 特化与 `destroyWithType` 类型分支留待 VulkanHandles 移植时按类型清单补齐
- use-after-free 检测点：`Acquire`（handle→SharedPtr）语义级检测、`Destroy` 入口 double-destroy 检测、HandleAllocator age 兜底（已有）、gc/terminate 泄漏计数（`BVK_DEBUG_RESOURCE_LEAK`）

## Capabilities

### New Capabilities

- `vulkan-resource`: 句柄资源的引用计数、延迟销毁（gc）、use-after-free 语义级检测

### Modified Capabilities

（无既有能力域在需求层面变化。`3rd/Utils` 的 Ref 扩展为第三方库改动，作为 vulkan-resource 的适配依赖写入 `specs/vulkan-resource/spec.md`，不在能力域注册表中单列）

## Impact

- 新增 `src/vulkan/resource/Resource.h/.cpp`、`ResourceManager.h/.cpp`，根 CMakeLists `file(GLOB_RECURSE)` 自动收集，无需改 CMake
- 修改 `3rd/Utils/include/Utils/mem/Ref.h`：新增 `protected virtual OnLastRef()`（默认 `delete this`）；既有 Ref 使用者（Driver、VulkanContext、VulInstance、VulPhysicalDevice、VulLogicDevice、VulQueue）行为不变
- 依赖：`src/HandleAllocator.h`（HandleAllocatorVK，已移植）、`include/Backend/Handle.h`、`Utils/mem/SharedPtr.h`、`Utils/Log.h`、`src/vulkan/VkDef.h`（`BVK_ENABLED`/`BVK_DEBUG_RESOURCE_LEAK`）
- 不依赖 Filament 任何头文件；不移植 ResourcePointer.h

# Tasks: port-vulkan-resource

## 1. 扩展 Utils::Ref（OnLastRef 虚回调）

- [x] 1.1 修改 `3rd/Utils/include/Utils/mem/Ref.h`：`SubRef()` 计数归零分支改为调用 `OnLastRef()`；新增 `protected: virtual void OnLastRef() { delete this; }`；`AddRef`/`GetRefCount`/拷贝移动禁止保持不动
- [x] 1.2 全量构建验证既有 Ref 使用者（Driver、VulkanContext、VulInstance、VulPhysicalDevice、VulLogicDevice、VulQueue）编译与行为无回归

## 2. 移植 Resource

- [x] 2.1 创建 `src/vulkan/resource/Resource.h`：
  - `ResourceType` 枚举完整移植 22 种类型 + `UNDEFINED_TYPE`（末位，UPPER_SNAKE 命名）
  - `template<typename D> ResourceType GetTypeEnum() noexcept` 主模板**定义**放头文件（返回 `UNDEFINED_TYPE`，未来特化在 Resource.cpp + 头文件声明）
  - `std::string_view GetTypeStr(ResourceType)` 声明
  - `struct Resource : public utils::Ref`：public `GetId()`/`GetResourceType()`；protected override `OnLastRef()`；private `resManager`/`id`/`restype`/销毁标记 + `Init<D>`/`SetDestroyed`/`IsDestroyed`；`friend class ResourceManager`
  - 头文件保护 `#pragma once`，include 顺序：本项目头 → Utils → 标准库
- [x] 2.2 创建 `src/vulkan/resource/Resource.cpp`：`GetTypeStr` 完整实现（22+1 枚举值 → 字符串）；`Resource::OnLastRef()` 实现为 `resManager->DestructLaterWithType(restype, id)`

## 3. 移植 ResourceManager

- [x] 3.1 创建 `src/vulkan/resource/ResourceManager.h`：
  - 构造函数 `(size_t arenaSize, bool disableUseAfterFreeCheck, bool disablePoolHandleTags)` 透传 `HandleAllocatorVK`
  - `template<typename D> Handle<D> AllocHandle()`（→ `mHandleAllocator.Allocate<D>()`）
  - `template<typename D, typename B, typename... ARGS> SharedPtr<D> Make(Handle<B> const&, ARGS&&...)`（→ `Construct` + `SharedPtr<D>`）
  - `template<typename D, typename... ARGS> SharedPtr<D> AllocateAndConstruct(ARGS&&...)`（分配 + 构造）
  - `template<typename D, typename B> SharedPtr<D> Acquire(Handle<B> const&)`：`HandleCast` + 销毁标记检查（`LOG_CRITICAL` 中止）→ 返回 `SharedPtr<D>`
  - `template<typename D> void Destroy(SharedPtr<D>&)`：double-destroy 检查 + 置标记 + `Reset()`
  - `AssociateTagToHandle(HandleBase::HandleId, utils::ImmutableString&&)`、`Gc()`、`Terminate()`、`Print()`
  - 私有：`Construct<D, B>`（`mHandleAllocator.Construct` + `Init<D>` + 泄漏计数）、`DestructLaterWithType`（持锁 push 单 GC 列表）、`DestroyWithType`、`traceConstruction`
  - 成员：`HandleAllocatorVK mHandleAllocator`、`std::mutex mGcListMutex`、`GcList mGcList`（`std::vector<std::pair<ResourceType, HandleBase::HandleId>>`）、debug 泄漏 `COUNTER`
- [x] 3.2 创建 `src/vulkan/resource/ResourceManager.cpp`：
  - 构造函数（透传三参数）
  - `Gc()`：锁内 swap 局部列表，锁外逐个 `DestroyWithType`；`Terminate()` 循环至空
  - `DestroyWithType`：骨架 switch 仅 `UNDEFINED_TYPE` 空分支（其余分支留待 VulkanHandles 移植，加注释说明）
  - `Print()`/`traceConstruction`：`BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)` 下 COUNTER 维护；`traceConstruction` 对 `UNDEFINED_TYPE` 用 `LOG_ASSERT`

## 4. 构建验证

- [x] 4.1 全量构建（`cmake --build build`）确认编译通过、链接成功（CMake `file(GLOB_RECURSE)` 自动收集新 cpp）
- [x] 4.2 构建 `tests` 确认无回归（既有 Ref 使用者编译、应用测试构建通过）

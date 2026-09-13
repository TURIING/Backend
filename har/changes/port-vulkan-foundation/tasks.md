# Tasks: port-vulkan-foundation

## 1. `3rd/Utils` 通用设施（子模块内，须单独提交，6 个头文件）

- [x] 1.1 `include/Utils/Hash.h`：`NS_UTILS::hash` 命名空间下的 `Murmur3(uint32_t const*, size_t, uint32_t)` / `MurmurSlow(uint8_t const*, size_t, uint32_t)` / 模板函子 `MurmurHashFn<T>`（含 `static_assert(sizeof(T) % 4 == 0)`）/ `Combine(size_t&, T const&)` / `CombineFast(size_t&, T const&)`
- [x] 1.2 `include/Utils/Bitset.h`：模板 `NS_UTILS::Bitset<T, N = 1>` + 别名 `Bitset8/32/64/128/256`；方法 `Set` / `Unset` / `Flip` / `Test` / `operator[]` / `GetBitsAt` / `GetValue` / `SetValue` / `Reset` / `Clear` / `Count` / `Any` / `None` / `All` / `ForEachSetBit` / `Size` / `Empty` / `FirstSetBit` + 位运算符；`Count` 用 `std::popcount`；编译期断言 `sizeof(Bitset32) == 4`、`sizeof(Bitset64) == 8`
- [x] 1.3 `include/Utils/Range.h`：`NS_UTILS::Range<T>`——私有 `m_first` / `m_last` + `First()` / `Last()` / `Size()` / `Empty()` / `Contains()` / `Overlaps()` + 随机访问 `const_iterator`
- [x] 1.4 `include/Utils/Panic.h`：`utils::details::Voidify` + `utils::PanicStream`（`std::ostringstream` 累积，析构时经 `utils::Log::Critical` 输出并 `abort`）+ `UTILS_CHECK_CONDITION_IMPL` / `FILAMENT_CHECK_PRECONDITION` / `FILAMENT_CHECK_POSTCONDITION`；**release 下仍生效**；以注释说明为何保留上游宏名
- [x] 1.5 `include/Utils/Debug.h`：`assert_invariant`——`NDEBUG` 下展开为 `((void)0)`，否则条件为假时输出 `__func__` / 文件 / 行号 / 条件字面量后中止；**不支持 `<<`**；**不依赖 `Utils/Log.h`**
- [x] 1.6 `include/Utils/RangeMap.h`：`NS_UTILS::RangeMap<KeyType, ValueType>`——`std::map<KeyType, std::pair<Range<KeyType>, ValueType>>` 存储 + `Add` / `Set` / `Has` / `Get`（无覆盖时 `FILAMENT_CHECK_PRECONDITION` 中止，**不返回默认值**）/ `Clear(first, last)` / `Reset(key)` / `RangeCount()`；私有 `Insert` / `Wipe` / `MergeRight` / `MergeLeft` / `Shrink` / `FindRange`
- [x] 1.7 `tests/`：为 `Bitset` / `Range` / `RangeMap` / `hash::MurmurHashFn` 补单元测试（覆盖各 spec 的 Scenario，含「相邻同值区间自动合并」「覆盖写入分裂既有区间」「`Clear` 裁剪边界」「无覆盖时中止」四条 RangeMap 语义）
- [x] 1.8 构建 `3rd/Utils` 并跑 `bin/UtilsTests`，全绿
- [ ] 1.9 在 `3rd/Utils` 仓库内单独提交（commit message 用中文、遵循 Conventional Commits、禁止出现"移植"字样）

## 2. 后端异步底座骨架

- [x] 2.1 `include/Backend/CallbackHandler.h`：定义 `Backend::CallbackHandler` 抽象类（`using Callback = void (*)(void*)` + 纯虚 `Post` + 虚析构）；以注释写明「后端只持指针、不拥有生命周期」
- [x] 2.2 `src/CallbackManager.h` / `CallbackManager.cpp`：实现句柄化回调登记与派发（`Get` / `SetCallback` / `Put` / 派发），构造接收 `Driver&`
- [x] 2.3 `src/CompilerThreadPool.h` / `CompilerThreadPool.cpp`：实现 `Init` / `Queue` / `Terminate`；等待用 `std::condition_variable`、任务体用 `std::function<void()>`；线程启动时调用 `JobSystem::SetThreadName` / `SetThreadPriority`
- [x] 2.4 `src/JobSystem.h`：定义 `Backend::JobSystem::SetThreadName` / `SetThreadPriority` 与 `Priority` 枚举；macOS 下 `SetThreadName` 调 `pthread_setname_np`，`SetThreadPriority` 空实现
- [x] 2.5 `src/DriverBase.h`：定义 `Backend::DriverBase : public Driver`，构造接收 `DriverConfig`，提供 `debugCommandBegin` 默认空实现
- [x] 2.6 全量构建，确认新增源文件被 `file(GLOB_RECURSE src/*.cpp)` 自动收集、无编译错误
- [x] 2.7 **实施中新增**：`include/Backend/Driver.h` 追加 5 个虚函数（`Purge` 纯虚 / `ScheduleCallback` 纯虚 / `SetUnrecoverableError` 默认空 / `DebugCommandBegin` 纯虚 / `DebugCommandEnd` 纯虚）——`DriverBase` 的编译前提；命名按项目 `PascalCase`，契约见 `driver-interface` delta spec
- [x] 2.8 **实施中新增**：`include/Backend/DriverDefine.h` 追加 `CompilerPriorityQueue`（`Critical` / `High` / `Low`）与 `COMPILER_PRIORITY_QUEUE_COUNT`——`CompilerThreadPool` 的三条优先级队列需要它；本属变更 2，因被本变更阻塞而前移
- [x] 2.9 **实施中新增**：`VulkanDriver` 改为继承 `DriverBase` 并在初始化列表传入 `DriverConfig`——否则新增的 4 个纯虚函数无人实现，构建立即失败；变更 7 本就要做这一步，提前落地以避免不可编译的中间态

## 3. ThreadSafeResource 回补

- [x] 3.1 `src/vulkan/resource/Resource.h`：新增 `struct ThreadSafeResource : public Resource {}`，以注释写明「语义标记，不引入第二套计数，归零仍走 `Resource::OnLastRef`」
- [x] 3.2 `src/vulkan/resource/Resource.h` / `ResourceManager.h`：新增 `isThreadSafeType(ResourceType)` 自由函数或静态成员，用 `CASE_FROM_TO` 覆盖 `Program` / `Fence` / `Sync` / `TimerQuery` 四条 + `default: return false`
- [x] 3.3 `src/vulkan/resource/ResourceManager.h`：新增 `m_threadSafeGcListMutex` / `m_threadSafeGcList` 成员（与既有 `m_gcListMutex` / `m_gcList` 成组放置）
- [x] 3.4 ~~`construct<D, B>` 增加 `if constexpr` 分支~~ **不适用**：`ThreadSafeResource` 派生自 `Resource`，`Init<D>` 为继承成员，`obj->Init<D>(...)` 对两类资源同样成立，无需分支（上游需分支是因为其 `ThreadSafeResource` 是平行基类）
- [x] 3.5 `src/vulkan/resource/ResourceManager.cpp`：`destructLaterWithType` 按 `isThreadSafeType(type)` 分派到对应列表并持对应的锁
- [x] 3.6 `src/vulkan/resource/ResourceManager.cpp`：`Gc()` 依次排空两个队列（先线程安全、后普通）；`Terminate()` 循环 `Gc()` 直至两个队列均为空
- [x] 3.7 复核 `Terminate()` 的循环条件确实同时检查两个队列（对照 spec 的「线程安全队列非空时 terminate 不提前返回」场景）

## 4. `EARLY_RETURN` 双份定义清理

- [x] 4.1 全项目检索 `EARLY_RETURN` 的使用点与 include 来源，列出只 include `"Macro.h"` 而未 include `Utils/Macro.h` 的文件
- [x] 4.2 删除 `src/Macro.h` 中的 `EARLY_RETURN` 定义；若该文件已空则一并删除，并修正只依赖它的 include
- [x] 4.3 全量构建确认无 `EARLY_RETURN` 未定义错误

## 5. `DriverConfig` 字段补齐

- [x] 5.1 `include/Backend/DriverDefine.h`：`DriverConfig` 增补 `disableHandleUseAfterFreeCheck` 与 `disableHeapHandleTags`（`ResourceManager` 构造已有这两个参数，但配置结构未暴露）
- [x] 5.2 复核 `VulkanDriver::Create` 的 `handleArenaSize` 兜底逻辑不受影响

## 6. 验证

- [x] 6.1 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 6.2 运行 `bin/BackendTests`（需 `DYLD_LIBRARY_PATH=/opt/homebrew/lib` 与 `VK_ICD_FILENAMES=<VulkanSDK>/macOS/share/vulkan/icd.d/MoltenVK_icd.json`）：8 帧往返正常、无 `LOG_CRITICAL`、exit 0
- [x] 6.3 运行 `bin/UtilsTests`：新增的 `Bitset` / `Range` / `RangeMap` / `hash::MurmurHashFn` 用例全绿
- [x] 6.4 静态复核：`isThreadSafeType` 覆盖的四个 `ResourceType` 与上游 `fvkmemory` 中继承 `ThreadSafeResource` 的类型一致（`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery`）
- [x] 6.5 静态复核：`ResourceManager` 双队列的加锁范围无嵌套（不在持 `m_gcListMutex` 时取 `m_threadSafeGcListMutex` 或反之），无死锁可能
- [x] 6.6 **记录局限**：`ThreadSafeResource` 的入队路径在本变更内无调用方（前四个类型属变更 6），运行时验证留待变更 6；本变更只以编译通过 + 分类表静态复核为准，SHALL NOT 宣称该路径已验证
- [x] 6.7 记录 `std::unordered_map` 替换约定的落地清单（7 个使用文件），供变更 5 / 6 引用
- [x] 6.8 记录**命名迁移清单**（PascalCase 决策的代价），供变更 4–7 引用：`forEachSetBit` → `ForEachSetBit`、`set`/`unset`/`test`/`count`/`reset`/`flip` → `Set`/`Unset`/`Test`/`Count`/`Reset`/`Flip`、`RangeMap::add`/`get`/`clear`/`rangeCount` → `Add`/`Get`/`Clear`/`RangeCount`、`Range::first`/`last` → `First()`/`Last()`、`hash::murmur3`/`combine` → `Murmur3`/`Combine`
- [x] 6.9 复核 `assert_invariant` 与 `FILAMENT_CHECK_*` 的差异已在两个头文件中写明（release 生效性 / 流式支持两条），避免变更 4–7 误用

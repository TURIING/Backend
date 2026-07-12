## 1. 环境准备

- [x] 1.1 添加 gtest git submodule 到 `3rd/Utils/3rd/gtest/`
- [x] 1.2 修改 `3rd/Utils/CMakeLists.txt`：添加 gtest 子目录、创建测试 target、链接 gtest
- [x] 1.3 创建 `3rd/Utils/tests/` 目录和测试入口文件 `main.cpp`

## 2. Ref 基类实现

- [x] 2.1 创建 `3rd/Utils/include/Utils/mem/Ref.h`：实现 `utils::Ref` 类（`AddRef`、`SubRef`、`GetRefCount`、virtual 析构、禁止拷贝/移动）

## 3. SharedPtr 智能指针实现

- [x] 3.1 创建 `3rd/Utils/include/Utils/mem/SharedPtr.h`：实现 `utils::SharedPtr<T>` 模板类（构造/拷贝/移动/赋值/析构/访问/Reset）
- [x] 3.2 实现 `MakeShared<T>(Args&&...)` 工厂函数

## 4. 测试

- [x] 4.1 编写 Ref 基类单元测试（初始计数、AddRef/SubRef 计数变化、归零销毁、禁止拷贝/移动）
- [x] 4.2 编写 SharedPtr 单元测试（默认构造、从裸指针接管、拷贝构造/赋值、移动构造/赋值、析构 SubRef、operator->/operator*、Reset、MakeShared、static_assert 类型约束）
- [x] 4.3 验证测试编译通过并全部通过

## 5. 多线程支持

- [x] 5.1 修改 `Ref.h`：`SubRef` 内存序从 `relaxed` 改为 `acq_rel`
- [x] 5.2 修改 `3rd/Utils/CMakeLists.txt`：UtilsTests 链接 `Threads::Threads`
- [x] 5.3 创建 `3rd/Utils/tests/MultithreadTest.cpp`：并发拷贝释放、生产者-消费者、压力测试、析构竞态
- [x] 5.4 验证所有测试（含多线程）编译通过并全部通过

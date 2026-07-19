## 1. 添加 VOLK Submodule

- [x] 1.1 添加 `3rd/volk` git submodule（`git submodule add https://github.com/zeux/volk.git 3rd/volk`）
- [x] 1.2 验证 submodule 克隆成功，`volk.h` 和 `volk.c` 文件存在

## 2. 配置 CMake 构建

- [x] 2.1 修改 `3rd/CMakeLists.txt`，添加 `volk` 静态库目标并追加到 `THIRD_PARTY_LIBS`
- [x] 2.2 修改根 `CMakeLists.txt`，为 `Backend` 目标添加 `VK_NO_PROTOTYPES` 编译定义并链接 `volk`
- [x] 2.3 构建验证：CMake 配置通过，Backend 库编译成功

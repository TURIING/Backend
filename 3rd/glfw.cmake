set(GLFW_ROOT ${CMAKE_CURRENT_LIST_DIR}/glfw)

# glfw 目标名称统一用变量，避免硬编码（见 .dsh/rules/code-style.md）
set(GLFW_TARGET_NAME glfw)

# 只构建库本身：文档、示例、测试与安装规则都不需要
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

add_subdirectory(${GLFW_ROOT})

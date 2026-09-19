#pragma once

#include "Utils/Macro.h"

#include <cstdint>

struct GLFWwindow;

namespace backend_test {

bool InitWindowing();

void TerminateWindowing();

// 创建不绑定任何图形 API 的窗口：Vulkan 上下文与本后端的交换链自行管理
NODISCARD GLFWwindow* CreateWindow(uint32_t width, uint32_t height, char const* title);

// 取窗口背后的 CAMetalLayer，供 vkCreateMetalSurfaceEXT 使用；失败返回 nullptr
NODISCARD void* GetMetalLayerFromWindow(GLFWwindow* window);

void DestroyWindow(GLFWwindow* window);

// 包一层事件接口，使 App 不必直接依赖 GLFW 头
NODISCARD bool WindowShouldClose(GLFWwindow* window);

void PollEvents();

// 帧缓冲像素尺寸：Retina 下是窗口逻辑尺寸的 2 倍，视口必须按它设置
void GetFramebufferSize(GLFWwindow* window, uint32_t& width, uint32_t& height);

}  // namespace backend_test

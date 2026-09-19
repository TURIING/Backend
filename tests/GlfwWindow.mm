#include "GlfwWindow.h"

#include "Utils/Log.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace backend_test {

bool InitWindowing() {
    if (glfwInit() != GLFW_TRUE) {
        LOG_ERROR("glfwInit failed");
        return false;
    }
    return true;
}

void TerminateWindowing() { glfwTerminate(); }

GLFWwindow *CreateWindow(uint32_t width, uint32_t height, char const *title) {
    // 后端自行管理 Vulkan 上下文，GLFW 不得创建任何图形 API 上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
}

void *GetMetalLayerFromWindow(GLFWwindow *window) {
    NSWindow *nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        LOG_ERROR("glfwGetCocoaWindow returned nil");
        return nullptr;
    }

    NSView *view = [nsWindow contentView];
    if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
        return view.layer;
    }

    // MoltenVK 的 vkCreateMetalSurfaceEXT 只接受 CAMetalLayer，GLFW 默认给的是普通 backing layer，
    // 故替换之；contentsScale 须取 backingScaleFactor，否则 Retina 下按 1x 渲染被放大
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.frame         = view.bounds;
    layer.contentsScale = nsWindow.backingScaleFactor;
    view.layer          = layer;
    view.wantsLayer     = YES;
    return layer;
}

void DestroyWindow(GLFWwindow *window) {
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
}

bool WindowShouldClose(GLFWwindow *window) {
    // Esc 关窗，省得演示时只能点标题栏按钮
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    return glfwWindowShouldClose(window) != 0;
}

void PollEvents() { glfwPollEvents(); }

void GetFramebufferSize(GLFWwindow *window, uint32_t &width, uint32_t &height) {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    width  = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
}

}  // namespace backend_test

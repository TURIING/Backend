#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/PixelBufferDescriptor.h"

#include "Utils/Macro.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include "vulkan/VkDef.h"

BEGIN_NS_BACKEND

struct VulkanRenderTarget;
struct VulkanTexture;

DECLARE_SHARE_PTR_CLASS(VulkanRenderTarget);
DECLARE_SHARE_PTR_CLASS(VulkanTexture);

// 像素读回。
//
// 读回在独立线程上执行（等待围栏、映射内存、拷贝数据），避免阻塞渲染线程。
//
// 生命周期约束：读回线程持有 VkDevice 与自建命令池，因此 terminate() 必须在 VkDevice
// 销毁前调用（不得晚于 VulkanDriver::DestroyResources 中销毁设备的那一步）。析构本身
// 不做清理，须显式调用 terminate()。
class VulkanReadPixels : public NS_UTILS::Ref {
public:
    // 在独立线程上执行任务的辅助类
    class TaskHandler {
    public:
        using WorkloadFunc   = std::function<void()>;
        using OnCompleteFunc = std::function<void()>;
        using Task           = std::pair<WorkloadFunc, OnCompleteFunc>;

        TaskHandler();

        // 除任务体外还须给出完成回调：任务执行完毕与被关闭时都会调用它，
        // 后者保证任务未执行时调用方仍能完成清理
        void Post(WorkloadFunc&& workload, OnCompleteFunc&& onComplete);

        // 阻塞至队列中全部任务执行完毕
        void Drain();

        // 不执行剩余任务体即退出，但完成回调仍会被调用
        void Shutdown();

    private:
        void Loop();

        bool                    m_shouldStop;
        std::condition_variable m_hasTaskCondition;
        std::mutex              m_taskQueueMutex;
        std::queue<Task>        m_taskQueue;
        std::thread             m_thread;
    };

    using OnReadCompleteFunction = std::function<void(PixelBufferDescriptor&&)>;
    using SelectMemoryFunction   = std::function<uint32_t(uint32_t, VkFlags)>;

    explicit VulkanReadPixels(VkDevice device);

    // 须在 VkDevice 销毁前调用；语义幂等
    void Terminate() noexcept;

    void Run(VulkanRenderTargetPtr srcTarget, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t graphicsQueueFamilyIndex,
             PixelBufferDescriptor&& pbd, SelectMemoryFunction const& selectMemoryFunc, OnReadCompleteFunction const& readCompleteFunc);

    void Run(VulkanTexturePtr srcTexture, uint8_t level, uint16_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
             uint32_t graphicsQueueFamilyIndex, PixelBufferDescriptor&& pbd, SelectMemoryFunction const& selectMemoryFunc,
             OnReadCompleteFunction const& readCompleteFunc);

    // 阻塞至全部在途请求完成
    void RunUntilComplete();

private:
    VkDevice   m_device = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::unique_ptr<TaskHandler> m_taskHandler;
};

DECLARE_SHARE_PTR_CLASS(VulkanReadPixels);

END_NS_BACKEND

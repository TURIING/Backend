#pragma once

#include "Backend/CallbackHandler.h"
#include "Backend/DriverDefine.h"
#include "Backend/Program.h"

#include "Utils/Utils.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "HwDefine.h"
#include "vulkan/VkDef.h"
#include "vulkan/resource/Resource.h"
#include "vulkan/sync/VulkanCmdFence.h"

BEGIN_NS_BACKEND

// push constant 的布局描述。
//
// 范围按 stage 顺序首尾相连地排在一段连续字节里：顶点在前、片元、计算在后，各 stage 的
// 起始偏移因此在本对象构造时算好并缓存，写入时不再重新推导。
struct PushConstantDescription {
    explicit PushConstantDescription(Program const& program);

    NODISCARD VkPushConstantRange const* GetVkRanges() const noexcept { return m_ranges; }

    NODISCARD uint32_t GetVkRangeCount() const noexcept { return m_rangeCount; }

    void Write(VkCommandBuffer cmdbuf, VkPipelineLayout layout, ShaderStage stage, uint8_t index,
               PushConstantVariant const& value);

private:
    static constexpr uint32_t kEntrySize = sizeof(uint32_t);

    struct ConstantDescription {
        std::vector<ConstantType> types;  // 各常量的声明类型，写入时据此校验取的是 variant 的哪个分支
        uint32_t                  offset = 0;
    };

    ConstantDescription m_descriptions[Program::SHADER_TYPE_COUNT];
    VkPushConstantRange m_ranges[Program::SHADER_TYPE_COUNT];
    uint32_t            m_rangeCount;
};

// VkShaderModule 的持有者。
//
struct VulkanProgram : public HwProgram, public Resource {
    VulkanProgram(VkDevice device, Program const& builder) noexcept;
    ~VulkanProgram() override;

    // 置位后，尚未执行的并行预编译任务据此跳过
    void CancelParallelCompilation() { m_parallelCompilationCanceled.store(true, std::memory_order_release); }

    NODISCARD bool IsParallelCompilationCanceled() const {
        return m_parallelCompilationCanceled.load(std::memory_order_acquire);
    }

    // 用已确定的布局写出全部排队中的 push constant
    void FlushPushConstants(VkPipelineLayout layout);

    void WritePushConstant(VkCommandBuffer cmdbuf, VkPipelineLayout layout, ShaderStage stage, uint8_t index,
                           PushConstantVariant const& value);

    NODISCARD VkShaderModule GetVertexShader() const { return m_info->shaders[0]; }

    NODISCARD VkShaderModule GetFragmentShader() const { return m_info->shaders[1]; }

    NODISCARD uint32_t GetPushConstantRangeCount() const { return m_info->pushConstantDescription.GetVkRangeCount(); }

    NODISCARD VkPushConstantRange const* GetPushConstantRanges() const {
        return m_info->pushConstantDescription.GetVkRanges();
    }

    static constexpr uint8_t kMaxShaderModules = 2;  // 前端到后端的着色器顺序是顶点、片元、计算，本项目只创建前两个

private:
    struct PipelineInfo {
        explicit PipelineInfo(Program const& program) noexcept : pushConstantDescription(program) {}

        VkShaderModule          shaders[kMaxShaderModules] = { VK_NULL_HANDLE };
        PushConstantDescription pushConstantDescription;
    };

    struct PushConstantInfo {
        VkCommandBuffer     cmdbuf;
        ShaderStage         stage;
        uint8_t             index;
        PushConstantVariant value;
    };

    PipelineInfo*                 m_info;
    VkDevice                      m_device = VK_NULL_HANDLE;
    std::atomic<bool>             m_parallelCompilationCanceled{ false };
    std::vector<PushConstantInfo> m_queuedPushConstants;
};
DECLARE_SHARE_PTR_CLASS(VulkanProgram);

struct VulkanFence : public HwFence, public Resource {
    VulkanFence() = default;

    void SetFence(std::shared_ptr<VulkanCmdFence> fence) { m_sharedFence = std::move(fence); }

    NODISCARD std::pair<std::shared_ptr<VulkanCmdFence>, bool> GetStatus() const {
        return { m_sharedFence, m_canceled };
    }

    void Cancel() const {
        if (m_sharedFence) {
            m_sharedFence->Cancel();
        }
        m_canceled = true;
    }

private:
    mutable bool                    m_canceled = false;
    std::shared_ptr<VulkanCmdFence> m_sharedFence;
};
DECLARE_SHARE_PTR_CLASS(VulkanFence);

// HwSync 的平台侧对象在创建时可能尚未就绪：转换回调先入队，待平台同步对象就绪后再派发
struct VulkanSync : public Resource, public HwSync {
    struct CallbackData {
        CallbackHandler*       handler;
        Platform::SyncCallback cb;
        Platform::Sync*        sync;
        void*                  userData;
    };

    VulkanSync() = default;

    // 与平台同步对象的创建/转换回调入队互斥
    NODISCARD std::mutex& GetLock() noexcept { return m_lock; }

    NODISCARD std::vector<std::unique_ptr<CallbackData>>& GetConversionCallbacks() noexcept {
        return m_conversionCallbacks;
    }

private:
    std::mutex                                 m_lock;
    std::vector<std::unique_ptr<CallbackData>> m_conversionCallbacks;
};
DECLARE_SHARE_PTR_CLASS(VulkanSync);

struct VulkanTimerQuery : public HwTimerQuery, public Resource {
    VulkanTimerQuery(uint32_t startingIndex, uint32_t stoppingIndex)
        : m_startingQueryIndex(startingIndex), m_stoppingQueryIndex(stoppingIndex) {}

    void SetFence(std::shared_ptr<VulkanCmdFence> fence) noexcept {
        std::lock_guard const lock(m_fenceMutex);
        m_fence = std::move(fence);
    }

    // 查询值同步读取可能先于 beginTimerQuery 把时间戳写进命令缓冲，此时读回的是无效值；
    // 故以「命令缓冲是否已完成」作为可读的判据，而不是依赖 AVAILABILITY_BIT
    NODISCARD bool IsCompleted() noexcept {
        std::lock_guard const lock(m_fenceMutex);
        return m_fence && m_fence->GetStatus() == VK_SUCCESS;
    }

    NODISCARD uint32_t GetStartingQueryIndex() const { return m_startingQueryIndex; }

    NODISCARD uint32_t GetStoppingQueryIndex() const { return m_stoppingQueryIndex; }

private:
    uint32_t                        m_startingQueryIndex;
    uint32_t                        m_stoppingQueryIndex;
    std::shared_ptr<VulkanCmdFence> m_fence;
    std::mutex                      m_fenceMutex;
};
DECLARE_SHARE_PTR_CLASS(VulkanTimerQuery);

END_NS_BACKEND

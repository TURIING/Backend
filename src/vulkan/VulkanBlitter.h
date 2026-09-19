#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Macro.h"

#include "vulkan/VkDef.h"
#include "vulkan/commands/VulkanCommands.h"

BEGIN_NS_BACKEND

struct VulkanAttachment;

// 图像拷贝与 MSAA 解析。
//
// 附件以值传递：VulkanAttachment 只是「纹理 + (level, layer)」的轻量视图。
class VulkanBlitter : public NS_UTILS::Ref {
public:
    VulkanBlitter(VkPhysicalDevice physicalDevice, const VulkanCommandsPtr& commands) noexcept;

    void Blit(VkFilter filter, VulkanAttachment dst, VkOffset3D const* dstRectPair, VulkanAttachment src, VkOffset3D const* srcRectPair);

    void Resolve(VulkanAttachment dst, VulkanAttachment src);

    void Terminate() noexcept;

private:
    [[maybe_unused]] VkPhysicalDevice m_physicalDevice;
    VulkanCommandsPtr m_commands;
};

DECLARE_SHARE_PTR_CLASS(VulkanBlitter);

END_NS_BACKEND

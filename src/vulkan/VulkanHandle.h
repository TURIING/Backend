#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/TargetBufferInfo.h"

#include "Utils/Bitset.h"
#include "Utils/Macro.h"
#include "Utils/Soa.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../HwDefine.h"
#include "VulkanFboCache.h"
#include "buffer/VulkanBufferProxy.h"
#include "resource/Resource.h"
#include "stage/VulkanStagePool.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/sync/VulkanCmdFence.h"

BEGIN_NS_BACKEND

struct VulkanCommandBuffer;
struct VulkanSwapChain;

DECLARE_SHARE_PTR_CLASS(VulkanSwapChain);

struct VulkanVertexBufferInfo : public HwVertexBufferInfo, public Resource {
    // 每位对应一个顶点属性下标，宽度须覆盖全部属性
    using AttributeBitSet = std::bitset<MAX_VERTEX_ATTRIBUTE_COUNT>;

    VulkanVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount, AttributeArray const& attributes);

    NODISCARD VkVertexInputAttributeDescription const* GetAttribDescriptions() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kAttributeDescription>();
    }

    NODISCARD VkVertexInputBindingDescription const* GetBufferDescriptions() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kBufferDescription>();
    }

    NODISCARD int8_t const* GetAttributeToBuffer() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kAttributeToBufferIndex>();
    }

    NODISCARD VkDeviceSize const* GetOffsets() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kOffsets>();
    }

    NODISCARD size_t GetAttributeCount() const noexcept { return m_info.m_soa.Size(); }

    NODISCARD AttributeBitSet GetDeclaredAttributes() const noexcept { return m_attributes; }

private:
    struct PipelineInfo {
        PipelineInfo(size_t size) : m_soa(size) { m_soa.Resize(size); }

        // 列下标即 SoA 模板实参序号
        static constexpr size_t kAttributeDescription   = 0;
        static constexpr size_t kBufferDescription      = 1;
        static constexpr size_t kOffsets                = 2;
        static constexpr size_t kAttributeToBufferIndex = 3;

        NS_UTILS::Soa<VkVertexInputAttributeDescription, VkVertexInputBindingDescription, VkDeviceSize, int8_t> m_soa;
    };

    AttributeBitSet m_attributes;
    PipelineInfo    m_info;
};
DECLARE_SHARE_PTR_CLASS(VulkanVertexBufferInfo);

struct VulkanBufferObject : public HwBufferObject, public Resource {
    VulkanBufferObject(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                       const VulkanBufferCachePtr& bufferCache, uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage);

    // 上传通道尚未移植：内容不会写入 GPU，调用方读到的是缓冲初始内容
    void LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes);

    NODISCARD VkBuffer GetVkBuffer() const noexcept { return m_buffer.GetVkBuffer(); }

    // 记录「本缓冲被该命令缓冲引用」，供缓冲回收判断是否需要屏障
    void ReferencedBy(VulkanCommandBuffer& commands) { m_buffer.ReferencedBy(commands); }

    BufferObjectBinding const bindingType;

private:
    VulkanBufferProxy m_buffer;
};
DECLARE_SHARE_PTR_CLASS(VulkanBufferObject);

struct VulkanIndexBuffer : public HwIndexBuffer, public Resource {
    VulkanIndexBuffer(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                      const VulkanBufferCachePtr& bufferCache, uint8_t elementSize, uint32_t indexCount);

    NODISCARD VkBuffer GetVkBuffer() const noexcept { return m_buffer.GetVkBuffer(); }

    void LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes);

    VkIndexType const indexType;

private:
    VulkanBufferProxy m_buffer;
};
DECLARE_SHARE_PTR_CLASS(VulkanIndexBuffer);

struct VulkanVertexBuffer : public HwVertexBuffer, public Resource {
    VulkanVertexBuffer(const VulkanContextPtr& context, const VulkanBufferCachePtr& bufferCache, uint32_t vertexCount,
                       VulkanVertexBufferInfoPtr vbi);

    void SetBuffer(const VulkanBufferObjectPtr& bufferObject, uint32_t index);

    // 缓冲代理可在运行期换掉底层 VkBuffer，故不缓存句柄作优化，只能取当前值
    NODISCARD VkBuffer const* GetVkBuffers() const noexcept { return m_buffers.data(); }
    NODISCARD VkBuffer* GetVkBuffers() noexcept { return m_buffers.data(); }

    // 声明的属性是否全部已挂上缓冲
    NODISCARD bool IsValid() const noexcept { return m_attributes == vbi->GetDeclaredAttributes(); }

    VulkanVertexBufferInfoPtr vbi;

private:
    std::vector<VkBuffer>           m_buffers;
    std::vector<VulkanBufferObjectPtr> m_resources;
    VulkanVertexBufferInfo::AttributeBitSet m_attributes;
};
DECLARE_SHARE_PTR_CLASS(VulkanVertexBuffer);

// 绘制图元：绑定顶点缓冲（必选）与索引缓冲（可选）的组合。
// 图元类型来自 HwRenderPrimitive，不必另存一份
struct VulkanRenderPrimitive : public HwRenderPrimitive, public Resource {
    VulkanRenderPrimitive(PrimitiveType primitiveType, VulkanVertexBufferPtr vertexBuffer, VulkanIndexBufferPtr indexBuffer);

    ~VulkanRenderPrimitive() override = default;

    VulkanVertexBufferPtr vertexBuffer;
    VulkanIndexBufferPtr  indexBuffer;
};
DECLARE_SHARE_PTR_CLASS(VulkanRenderPrimitive);

// 描述符集布局的资源对象：位掩码 + 各类描述符计数由后端布局描述一次性推导。
//
// 位掩码的低半区归顶点阶段、高半区归片元阶段，据此可把两阶段的绑定合并成一条 Vulkan 绑定。
struct VulkanDescriptorSetLayout : public HwDescriptorSetLayout, public Resource {
    static constexpr uint8_t kUniqueDescriptorSetCount = 4;
    static constexpr uint8_t kMaxBindings             = 25;

    using DescriptorSetLayoutArray = std::array<VkDescriptorSetLayout, kUniqueDescriptorSetCount>;

    struct Bitmask {
        VK_UTILS::UniformBufferBitmask   ubo;              // 8 字节
        VK_UTILS::UniformBufferBitmask   dynamicUbo;       // 8 字节
        VK_UTILS::SamplerBitmask         sampler;          // 8 字节
        VK_UTILS::InputAttachmentBitmask inputAttachment;  // 8 字节

        // sampler 的子集：哪些采样器来自外部图像的不可变采样器
        VK_UTILS::SamplerBitmask externalSampler;  // 8 字节

        bool operator==(Bitmask const& right) const {
            return ubo == right.ubo && dynamicUbo == right.dynamicUbo && sampler == right.sampler &&
                   inputAttachment == right.inputAttachment && externalSampler == right.externalSampler;
        }

        NODISCARD static Bitmask FromLayoutDescription(DescriptorSetLayout const& layout);
    };
    static_assert(sizeof(Bitmask) == 40);

    // 供描述符池按「同一组描述符计数」匹配合并池
    struct Count {
        uint32_t ubo             = 0;
        uint32_t dynamicUbo      = 0;
        uint32_t sampler         = 0;
        uint32_t inputAttachment = 0;

        NODISCARD uint32_t Total() const { return ubo + dynamicUbo + sampler + inputAttachment; }

        bool operator==(Count const& right) const noexcept {
            return ubo == right.ubo && dynamicUbo == right.dynamicUbo && sampler == right.sampler &&
                   inputAttachment == right.inputAttachment;
        }

        NODISCARD static Count FromLayoutBitmask(Bitmask const& mask) {
            return {
                .ubo             = CollapsedCount(mask.ubo),
                .dynamicUbo      = CollapsedCount(mask.dynamicUbo),
                .sampler         = CollapsedCount(mask.sampler),
                .inputAttachment = CollapsedCount(mask.inputAttachment),
            };
        }

        NODISCARD Count operator*(uint16_t mult) const noexcept {
            Count ret;
            ret.ubo             = ubo * mult;
            ret.dynamicUbo      = dynamicUbo * mult;
            ret.sampler         = sampler * mult;
            ret.inputAttachment = inputAttachment * mult;
            return ret;
        }
    };

    VulkanDescriptorSetLayout(DescriptorSetLayout&& layout, VkDescriptorSetLayout vkLayout);

    // vkLayout 的生命周期归 layout cache，本对象只借用
    ~VulkanDescriptorSetLayout() override = default;

    NODISCARD VkDescriptorSetLayout GetVkLayout() const noexcept { return m_vkLayout; }

    NODISCARD VkDescriptorSetLayout GetExternalSamplerVkLayout() const noexcept { return m_externalSamplerVkLayout; }

    void SetExternalSamplerVkLayout(VkDescriptorSetLayout vkLayout) noexcept { m_externalSamplerVkLayout = vkLayout; }

    NODISCARD bool HasExternalSamplers() const noexcept { return bitmask.externalSampler.Count() > 0; }

    Bitmask const bitmask;
    Count const   count;

private:
    // 顶点与片元两阶段的位被折叠到低半区后统计，得到「每个绑定占一个描述符」的个数
    template <typename Bitmask>
    NODISCARD static uint8_t CollapsedCount(Bitmask const& mask) {
        static_assert(sizeof(mask) <= 64);
        constexpr uint8_t  kShift        = VK_UTILS::GetFragmentStageShift<Bitmask>();
        constexpr uint64_t kVertexMask   = (1ULL << kShift) - 1ULL;
        constexpr uint64_t kFragmentMask = kVertexMask << kShift;
        uint64_t           val           = mask.GetValue();
        val = ((val & kVertexMask) >> VK_UTILS::GetVertexStageShift<Bitmask>()) | ((val & kFragmentMask) >> kShift);
        return static_cast<uint8_t>(Bitmask(val).Count());
    }

    // 不带不可变采样器的布局
    VkDescriptorSetLayout const m_vkLayout = VK_NULL_HANDLE;

    // 带不可变采样器的布局，可被外部图像路径改写
    VkDescriptorSetLayout m_externalSamplerVkLayout = VK_NULL_HANDLE;
};
DECLARE_SHARE_PTR_CLASS(VulkanDescriptorSetLayout);

// 描述符集的资源对象。
//
// 一个逻辑描述符集背后可能有多个 VkDescriptorSet（切换布局时会新建一个），
// m_currentSetIndex 指向当前生效的那个。
struct VulkanDescriptorSet : public HwDescriptorSet, public Resource {
    // 未被使用的 VkDescriptorSet 需要归还池，故由池注入回收回调
    using OnRecycle = std::function<void(VulkanDescriptorSet*)>;

    VulkanDescriptorSet(VulkanDescriptorSetLayoutPtr layout, OnRecycle&& onRecycleFn, VkDescriptorSet vkSet);

    ~VulkanDescriptorSet() override;

    NODISCARD VkDescriptorSet GetVkSet() const noexcept { return m_sets[m_currentSetIndex].vkSet; }

    void SetOffsets(DescriptorSetOffsetArray&& offsets) noexcept { m_offsets = std::move(offsets); }

    NODISCARD DescriptorSetOffsetArray const* GetOffsets() { return &m_offsets; }

    // 登记本集合引用的资源：借出一份引用，保证提交完成前不被回收；
    // 记录 ubo 位置是后续判断是否需要写屏障的依据
    template <typename T, typename = std::enable_if_t<std::is_base_of_v<Resource, T>>>
    void Acquire(NS_UTILS::SharedPtr<T> resource) {
        if (resource->template IsType<VulkanBufferObject>()) {
            m_uboMask.Set(m_resources.size());
        }
        m_resources.emplace_back(resource.Get());
    }

    void ReferencedBy(VulkanCommandBuffer& commands);

    NODISCARD bool IsBound() const { return bool(m_sets[m_currentSetIndex].fenceStatus); }

    NODISCARD VulkanDescriptorSetLayoutPtr GetLayout() const { return m_layout; }

    // 当前生效的布局：可能与 mLayout 给出的默认布局不同（绑定了外部采样器时会换成另一份）
    VkDescriptorSetLayout boundLayout = VK_NULL_HANDLE;

    VK_UTILS::UniformBufferBitmask const& dynamicUboMask;
    uint8_t const                        uniqueDynamicUboCount;

    // 仅在绑定了外部采样器图像时需要重建布局
    bool isLayoutDirty            = false;
    bool isAnExternalSamplerBound = false;

private:
    friend class VulkanDescriptorSetCache;

    void AddNewSet(VkDescriptorSet vkSet, OnRecycle&& onRecycleFn);

    void Gc();

    struct InternalVkSet {
        VkDescriptorSet                 vkSet = VK_NULL_HANDLE;
        OnRecycle                       onRecycleFn;
        std::shared_ptr<VulkanCmdFence> fenceStatus;
    };

    VulkanDescriptorSetLayoutPtr m_layout;
    DescriptorSetOffsetArray     m_offsets;
    std::vector<ResourcePtr>     m_resources;
    uint8_t                      m_currentSetIndex;
    std::vector<InternalVkSet>   m_sets;
    VK_UTILS::UniformBufferBitmask m_uboMask;
};
DECLARE_SHARE_PTR_CLASS(VulkanDescriptorSet);

// 渲染目标的附件集合，以及由附件推导出的渲染通道键与帧缓冲键。
//
// 附件的图像有两种归属：交换链持有（m_offscreen == false）或纹理持有（m_offscreen == true）。
// 私有继承 HwRenderTarget 是为了挡住其中的 width / height —— 作为默认渲染目标时它们
// 并不代表真实尺寸，尺寸须经交换链绑定后才有效。
struct VulkanRenderTarget : private HwRenderTarget, public Resource {
    // 离屏渲染目标
    VulkanRenderTarget(VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager,
                       VmaAllocator allocator, VulkanCommands* commands, uint32_t width, uint32_t height, uint8_t samples,
                       VulkanAttachment color[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT], VulkanAttachment depthStencil,
                       const VulkanStagePoolPtr& stagePool, uint8_t layerCount);

    ~VulkanRenderTarget() override;

    // 默认渲染目标：构造期即存在，附件由 BindSwapChain 在 makeCurrent 时注入
    VulkanRenderTarget();

    VulkanRenderTarget(VulkanRenderTarget&& target) noexcept : HwRenderTarget(0, 0), m_offscreen(false), m_protected(false) {
        Swap(std::move(target));
    }

    VulkanRenderTarget& operator=(VulkanRenderTarget&& target) noexcept {
        Swap(std::move(target));
        return *this;
    }

    VulkanRenderTarget(VulkanRenderTarget const&)            = delete;
    VulkanRenderTarget& operator=(VulkanRenderTarget const&) = delete;

    void TransformClientRectToPlatform(VkRect2D* bounds) const;

    void TransformViewportToPlatform(VkViewport* bounds) const;

    NODISCARD VkExtent2D GetExtent() const noexcept { return { width, height }; }

    // 颜色附件清空值的类型族。清空时据此选择 VkClearColorValue 的 union 分支，需在附件
    // 绑定时算好并缓存，避免清空路径每次重新判定格式
    enum class ColorClearKind : uint8_t { Float, SignedInt, UnsignedInt };

    // 颜色附件在 attachments 里紧凑排布在下标 0 起的位置，第 idx 个附件不能直接按下标取
    NODISCARD VulkanAttachment& GetColor(uint32_t idx) const {
        LOG_ASSERT(idx < m_info->colors.Count());
        return m_info->attachments[idx];
    }

    NODISCARD ColorClearKind GetColorClearKind(uint32_t idx) const {
        LOG_ASSERT(idx < m_info->colors.Count());
        return m_info->colorClearKinds[idx];
    }

    NODISCARD VulkanAttachment& GetDepthStencil() const {
        LOG_ASSERT(HasDepthStencil());
        if (m_info->fbkey.samples == 1) {
            return m_info->attachments[m_info->depthStencilIndex];
        }
        return m_info->attachments[m_info->msaaDepthStencilIndex];
    }

    NODISCARD VulkanFboCache::RenderPassKey const& GetRenderPassKey() const noexcept { return m_info->rpkey; }

    NODISCARD VulkanFboCache::FboKey const& GetFboKey() const noexcept { return m_info->fbkey; }

    NODISCARD uint8_t GetSamples() const noexcept { return m_info->fbkey.samples; }

    NODISCARD uint8_t GetColorTargetCount(VulkanRenderPassContext const& pass) const;

    NODISCARD bool HasDepthStencil() const noexcept { return m_info->depthStencilIndex != Auxiliary::kUndefinedIndex; }

    NODISCARD bool IsSwapChain() const noexcept { return !m_offscreen; }

    NODISCARD bool IsProtected() const noexcept { return m_protected; }

    void BindSwapChain(VulkanSwapChainPtr swapchain);

    void ReleaseSwapchain();

    NODISCARD bool IsSwapchainBound() const noexcept { return IsSwapChain() && m_info->colors[0]; }

    void EmitBarriersBeginRenderPass(VulkanCommandBuffer& commands);

    void EmitBarriersEndRenderPass(VulkanCommandBuffer& commands);

private:
    // 用 unique_ptr 而非内联成员：迁移语义经 swap() 一次指针交换完成，不必逐字段搬运
    // 含 16 个图像视图的 fbkey
    struct Auxiliary {
        static constexpr int8_t kUndefinedIndex = -1;

        VulkanFboCache::RenderPassKey rpkey = {};
        VulkanFboCache::FboKey        fbkey = {};
        std::vector<VulkanAttachment> attachments;
        // 与 attachments[0..colors.Count()-1] 一一对应：颜色附件的清空值类型族
        std::array<ColorClearKind, MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT> colorClearKinds = {};
        NS_UTILS::Bitset32                                                 colors;
        int8_t                                                             depthStencilIndex     = kUndefinedIndex;
        int8_t                                                             msaaDepthStencilIndex = kUndefinedIndex;
        int8_t                                                             msaaIndex             = kUndefinedIndex;
    };

    void Swap(VulkanRenderTarget&& target) {
        std::swap(width, target.width);
        std::swap(height, target.height);
        std::swap(m_offscreen, target.m_offscreen);
        std::swap(m_protected, target.m_protected);
        std::swap(m_info, target.m_info);
    }

    bool m_offscreen;
    bool m_protected;

    std::unique_ptr<Auxiliary> m_info;
};

// 内存映射缓冲：只是「缓冲对象的一段窗口」的视图，故不持有 VkBuffer，只记句柄与区间
struct VulkanMemoryMappedBuffer : public HwMemoryMappedBuffer, public Resource {
    VulkanMemoryMappedBuffer(BufferObjectHandle boh, size_t offset, size_t size, MapBufferAccessFlags access)
        : boh(boh), access(access), size(size), offset(offset) {}

    BufferObjectHandle const    boh;
    MapBufferAccessFlags const  access;
    uint32_t const              size;
    uint32_t const              offset;
};
DECLARE_SHARE_PTR_CLASS(VulkanMemoryMappedBuffer);

END_NS_BACKEND

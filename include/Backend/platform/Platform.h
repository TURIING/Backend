#pragma once

#include "Backend/Namespace.h"

#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <cstddef>
#include <cstdint>
#include <memory>

BEGIN_NS_BACKEND

// 只取 DriverPtr 别名，避免 Platform.h 反向包含 Driver.h 形成循环
DECLARE_CLASS_AND_SHARE_PTR(Driver);

// 命令缓冲层维护的围栏状态；此处仅前置声明，公共头不依赖 src/
struct VulkanCmdFence;

/**
 * @brief 平台抽象基类：创建设备，并暴露交换链、同步句柄与呈现时序查询
 */
class UTILS_PUBLIC Platform : public utils::Ref {
public:
    // 不透明句柄，具体实现由平台子类派生
    struct SwapChain {};
    struct Fence {};
    struct Stream {};
    struct Sync {};

    using SyncCallback = void (*)(Sync *sync, void *userData);

    class ExternalImageHandle;

    // 外部图像基类。
    //
    // 本项目的外部图像路径已按决策砍掉（驱动侧只有返回空句柄的空桩），故这里只保留类型骨架：
    // 引用计数接口的真实语义随平台实现一起被略去，句柄不可构造出非空实例。
    class ExternalImage {
    protected:
        virtual ~ExternalImage() noexcept = default;
    };

    class ExternalImageHandle {
    public:
        ExternalImageHandle() noexcept = default;

        explicit ExternalImageHandle(ExternalImage *p) noexcept : mTarget(p) {}

        bool operator==(ExternalImageHandle const &rhs) const noexcept { return mTarget == rhs.mTarget; }

        explicit operator bool() const noexcept { return mTarget != nullptr; }

        NODISCARD ExternalImage *Get() noexcept { return mTarget; }
        NODISCARD ExternalImage const *Get() const noexcept { return mTarget; }

        void Clear() noexcept { mTarget = nullptr; }

    private:
        ExternalImage *mTarget = nullptr;
    };

    using ExternalImageHandleRef = ExternalImageHandle const &;

    struct CompositorTiming {
        // 时间戳与时长均为纳秒，基准是 std::steady_clock
        using time_point_ns = int64_t;
        using duration_ns   = int64_t;

        static constexpr time_point_ns INVALID = -1;  //!< 平台不支持该值

        duration_ns compositeInterval;         //!< 相邻两次合成事件的时间间隔
        duration_ns compositeDeadlineLatency;  //!< 合成开始到下次合成开始，即新帧入队的截止时间
        duration_ns compositeToPresentLatency; //!< 合成开始到该次合成的预期呈现时刻
        duration_ns expectedPresentLatency;    //!< 呈现相对 vsync 的预期延迟
    };

    struct FrameTimestamps {
        using time_point_ns = int64_t;

        static constexpr time_point_ns INVALID = -1;  //!< 平台不支持该值
        static constexpr time_point_ns PENDING = -2;  //!< 该值尚不可用

        time_point_ns requestedPresentTime;      //!< 应用请求呈现的时刻；未显式指定时为入队时刻
        time_point_ns acquireTime;               //!< 应用完成对该 surface 渲染的时刻
        time_point_ns latchTime;                 //!< 合成器选中该帧用于下次合成的时刻
        time_point_ns firstCompositionStartTime; //!< 首次开始准备该帧合成的时刻；由显示硬件合成时为 0
        time_point_ns lastCompositionStartTime;  //!< 末次开始准备该帧合成的时刻
        time_point_ns gpuCompositionDoneTime;    //!< 合成器完成该帧渲染的时刻；未参与渲染时为 INVALID
        time_point_ns displayPresentTime;        //!< 该帧开始扫描输出到物理显示器的时刻
        time_point_ns dequeueReadyTime;          //!< 缓冲可被客户端无阻塞复用的时刻
        time_point_ns releaseTime;               //!< 该帧的全部读取（显示/合成用途）完成的时刻
    };

    /**
     * 立体渲染技术；所用材质需与所选技术兼容
     */
    enum class StereoscopicType : uint8_t {
        None,       //!< 不启用立体渲染
        Instanced,  //!< 使用 instanced 渲染
        Multiview,  //!< 使用图形后端的多视图特性
    };

    /**
     * GPU 上下文优先级，控制 GPU 工作调度与抢占
     */
    enum class GpuContextPriority : uint8_t {
        Default,   //!< 后端默认（通常为 Medium）
        Low,       //!< 非交互、可延迟负载
        Medium,    //!< 标准应用的默认级别
        High,      //!< 高优先级、延迟敏感负载
        Realtime,  //!< 系统关键实时应用（如 VR/AR compositor）
    };

    /**
     * 异步操作的处理方式
     */
    enum class AsynchronousMode : uint8_t {
        None,             //!< 禁用异步操作（默认）
        ThreadPreferred,  //!< 优先用专用工作线程；平台不支持线程时退化为摊销策略
        Amortization,     //!< 每次引擎更新摊销处理少量异步任务
    };

    struct DriverConfig {
        // FeatureFlagManager 尚未移植，先以 void const* 占位，变更 7 建立该类型后改强类型
        void const *featureFlagManager = nullptr;

        // 句柄 arena 大小；0 表示由驱动按有效区间取默认值
        size_t handleArenaSize = 0;

        size_t metalUploadBufferSizeBytes = 512 * 1024;

        bool disableParallelShaderCompile   = false;
        bool disableAmortizedShaderCompile  = true;
        bool disableHandleUseAfterFreeCheck = false;
        bool disableHeapHandleTags          = false;
        bool forceGLES2Context              = false;

        StereoscopicType stereoscopicType = StereoscopicType::None;

        // 启用立体渲染时渲染的眼睛数，取值 1 到 Engine::GetMaxStereoscopicEyes()
        uint8_t stereoscopicEyeCount = 2;

        bool               assertNativeWindowIsValid          = false;
        bool               metalDisablePanicOnDrawableFailure = false;
        GpuContextPriority gpuContextPriority                 = GpuContextPriority::Default;

        bool vulkanEnableAsyncPipelineCachePrewarming = false;
        bool vulkanEnableStagingBufferBypass          = false;

        AsynchronousMode asynchronousMode = AsynchronousMode::None;
    };

    ~Platform() noexcept override = default;

    virtual DriverPtr CreateDriver(const DriverConfig &config, void *shareContext) = 0;

    /**
     * @brief 在平台的主事件线程上处理事件队列；返回 false 表示无需特殊处理
     */
    virtual bool PumpEvents() noexcept { return false; }

    /**
     * @brief 该平台是否支持合成器时序查询
     */
    NODISCARD virtual bool IsCompositorTimingSupported() const noexcept { return false; }

    /**
     * @brief 查询合成器时序；交换链的原生窗口须有效（headless 交换链查询恒失败）
     */
    virtual bool QueryCompositorTiming(SwapChain const *, CompositorTiming *) const noexcept { return false; }

    /**
     * @brief 把单调递增的 frameId 关联到该交换链上将要呈现的下一帧；须在渲染线程调用
     */
    virtual bool SetPresentFrameId(SwapChain const *, uint64_t) noexcept { return false; }

    /**
     * @brief 查询指定帧的时序；系统只保留有限帧数的历史
     */
    virtual bool QueryFrameTimestamps(SwapChain const *, uint64_t, FrameTimestamps *) const noexcept { return false; }

    /**
     * @brief 创建 Platform::Sync 对象，承载给定围栏的状态以供转换为外部同步对象
     */
    virtual Sync *CreateSync(std::shared_ptr<VulkanCmdFence> fenceStatus) noexcept { return nullptr; }

    /**
     * @brief 销毁由本平台 CreateSync 创建的同步对象
     */
    virtual void DestroySync(Sync *sync) noexcept {}
};

DECLARE_SHARE_PTR_CLASS(Platform);

END_NS_BACKEND

#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/TargetBufferInfo.h"

#include "VkDef.h"

BEGIN_NS_BACKEND

// 单个渲染通道可能出现的附件总数：颜色附件 + 其 MSAA 解析附件 + 深度/模板附件。
// 缓存创建 VkRenderPass / VkFramebuffer 时按此值预留定长数组。
constexpr uint8_t kMaxRenderTargetAttachmentTextures = MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1;

// 未使用的管线要在缓存里滞留多少次提交才允许销毁。
//
// 管线缓存不跟踪各命令缓冲分别引用了哪些管线，只按「提交次数」估算：取值不小于命令缓冲
// 上限，才能保证引用过某管线的提交都已结束。
constexpr int kMaxPipelineAge = kMaxCommandBuffers;

static_assert(kMaxPipelineAge >= kMaxCommandBuffers);

// 以 RenderDoc 捕获为目标时须关掉其不支持的特性，否则捕获时驱动会崩溃
constexpr bool kRenderdocCaptureMode = false;

END_NS_BACKEND

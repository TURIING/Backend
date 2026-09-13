#ifndef DECL_DRIVER_API
#error "The DECL_DRIVER_API() macro must be defined before including this file"
#define DECL_DRIVER_API(M, D, P)
#endif

#ifndef DECL_DRIVER_API_SYNCHRONOUS
#error "The DECL_DRIVER_API_SYNCHRONOUS() macro must be defined before including this file"
#define DECL_DRIVER_API_SYNCHRONOUS(R, M, D, P)
#endif

#ifndef DECL_DRIVER_API_RETURN
#error "The DECL_DRIVER_API_RETURN() macro must be defined before including this file"
#define DECL_DRIVER_API_RETURN(R, M, D, P)
#endif

#define EXPAND(x) x

#define APPLY0(M, ...)
#define APPLY1(M, A, ...)                                                             EXPAND(M(A))
#define APPLY2(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY1(M, __VA_ARGS__))
#define APPLY3(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY2(M, __VA_ARGS__))
#define APPLY4(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY3(M, __VA_ARGS__))
#define APPLY5(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY4(M, __VA_ARGS__))
#define APPLY6(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY5(M, __VA_ARGS__))
#define APPLY7(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY6(M, __VA_ARGS__))
#define APPLY8(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY7(M, __VA_ARGS__))
#define APPLY9(M, A, ...)                                                             EXPAND(M(A)), EXPAND(APPLY8(M, __VA_ARGS__))
#define APPLY10(M, A, ...)                                                            EXPAND(M(A)), EXPAND(APPLY9(M, __VA_ARGS__))
#define APPLY11(M, A, ...)                                                            EXPAND(M(A)), EXPAND(APPLY10(M, __VA_ARGS__))
#define APPLY12(M, A, ...)                                                            EXPAND(M(A)), EXPAND(APPLY11(M, __VA_ARGS__))
#define APPLY13(M, A, ...)                                                            EXPAND(M(A)), EXPAND(APPLY12(M, __VA_ARGS__))
#define APPLY_N__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, X, ...) APPLY##X
#define APPLY(M, ...)                                                                 EXPAND(EXPAND(APPLY_N__(M, __VA_ARGS__, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))(M, __VA_ARGS__))

#define PAIR_ARGS_0(M, ...)
#define PAIR_ARGS_1(M, X, Y, ...)  M(X, Y)
#define PAIR_ARGS_2(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_1(M, __VA_ARGS__))
#define PAIR_ARGS_3(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_2(M, __VA_ARGS__))
#define PAIR_ARGS_4(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_3(M, __VA_ARGS__))
#define PAIR_ARGS_5(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_4(M, __VA_ARGS__))
#define PAIR_ARGS_6(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_5(M, __VA_ARGS__))
#define PAIR_ARGS_7(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_6(M, __VA_ARGS__))
#define PAIR_ARGS_8(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_7(M, __VA_ARGS__))
#define PAIR_ARGS_9(M, X, Y, ...)  M(X, Y), EXPAND(PAIR_ARGS_8(M, __VA_ARGS__))
#define PAIR_ARGS_10(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_9(M, __VA_ARGS__))
#define PAIR_ARGS_11(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_10(M, __VA_ARGS__))
#define PAIR_ARGS_12(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_11(M, __VA_ARGS__))
#define PAIR_ARGS_13(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_12(M, __VA_ARGS__))

#define PAIR_ARGS_N__(_0, E1, _1, E2, _2, E3, _3, E4, _4, E5, _5, E6, _6, E7, _7, E8, _8, E9, _9, E10, _10, E11, _11, E12, _12, E13, _13, X, ...) \
    PAIR_ARGS_##X

#define PAIR_ARGS_N(M, ...)                                                                                                              \
    EXPAND(EXPAND(PAIR_ARGS_N__(0, ##__VA_ARGS__, 13, E, 12, E, 11, E, 10, E, 9, E, 8, E, 7, E, 6, E, 5, E, 4, E, 3, E, 2, E, 1, E, 0))( \
        M, __VA_ARGS__))

#define ARG(T, P) T P

#define PARAM(T, P) P

#define DECL_DRIVER_API_N(N, ...) DECL_DRIVER_API(N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))

#define DECL_DRIVER_API_R_N(R, N, ...) DECL_DRIVER_API_RETURN(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))

#define DECL_DRIVER_API_TAGGED_R_N(R, N, ...)                                                            \
    DECL_DRIVER_API_RETURN(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__, NS_UTILS::ImmutableString&&, tag = {}), \
                           PAIR_ARGS_N(PARAM, ##__VA_ARGS__, NS_UTILS::ImmutableString&&, tag))

#define DECL_DRIVER_API_SYNCHRONOUS_N(R, N, ...) DECL_DRIVER_API_SYNCHRONOUS(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))

#define DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N(R, N, ...)                                                     \
    DECL_DRIVER_API_SYNCHRONOUS(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__, NS_UTILS::ImmutableString, tag = {}), \
                                PAIR_ARGS_N(PARAM, ##__VA_ARGS__, NS_UTILS::ImmutableString, tag))

#define DECL_DRIVER_API_SYNCHRONOUS_0(R, N) DECL_DRIVER_API_SYNCHRONOUS(R, N, , )

#define PARAM_1(P, ...)           P
#define ARG_1(T, P, ...)          T P
#define DECL_DRIVER_API_0(N)      DECL_DRIVER_API(N, ARG_1(int, dummy = 0), PARAM_1(dummy))
#define DECL_DRIVER_API_R_0(R, N) DECL_DRIVER_API_RETURN(R, N, ARG_1(int, dummy = 0), PARAM_1(dummy))

DECL_DRIVER_API_0(tick)

DECL_DRIVER_API_N(beginFrame, int64_t, monotonic_clock_ns, int64_t, refreshIntervalNs, uint32_t, frameId)

DECL_DRIVER_API_N(SetFrameScheduledCallback, SwapChainHandle, sch, CallbackHandler*, handler, FrameScheduledCallback&&, callback, uint64_t, flags)

DECL_DRIVER_API_N(SetFrameCompletedCallback, SwapChainHandle, sch, CallbackHandler*, handler, std::function<void()>&&, callback)

DECL_DRIVER_API_N(SetPresentationTime, int64_t, monotonic_clock_ns)

DECL_DRIVER_API_N(EndFrame, uint32_t, frameId)

DECL_DRIVER_API_0(flush)

DECL_DRIVER_API_0(finish)

DECL_DRIVER_API_0(resetState)

DECL_DRIVER_API_TAGGED_R_N(VertexBufferInfoHandle, CreateVertexBufferInfo, uint8_t, bufferCount, uint8_t, attributeCount, AttributeArray, attributes)

DECL_DRIVER_API_TAGGED_R_N(VertexBufferHandle, CreateVertexBuffer, uint32_t, vertexCount, VertexBufferInfoHandle, vbInfoHandle)

DECL_DRIVER_API_TAGGED_R_N(VertexBufferHandle, CreateVertexBufferAsync, uint32_t, vertexCount, VertexBufferInfoHandle, vbih, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_TAGGED_R_N(IndexBufferHandle, CreateIndexBuffer, ElementType, elementType, uint32_t, indexCount, BufferUsage, usage)

DECL_DRIVER_API_TAGGED_R_N(IndexBufferHandle, CreateIndexBufferAsync, ElementType, elementType, uint32_t, indexCount, BufferUsage, usage, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_TAGGED_R_N(BufferObjectHandle, CreateBufferObject, uint32_t, byteCount, BufferObjectBinding, bindingType, BufferUsage, usage)

DECL_DRIVER_API_TAGGED_R_N(BufferObjectHandle, CreateBufferObjectAsync, uint32_t, byteCount, BufferObjectBinding, bindingType, BufferUsage, usage, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTexture, SamplerType, target, uint8_t, levels, TextureFormat, format, uint8_t, samples, uint32_t, width, uint32_t, height, uint32_t, depth, TextureUsage, usage)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureAsync, SamplerType, target, uint8_t, levels, TextureFormat, format, uint8_t, samples, uint32_t, width, uint32_t, height, uint32_t, depth, TextureUsage, usage, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureView, TextureHandle, texture, uint8_t, baseLevel, uint8_t, levelCount)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureViewSwizzle, TextureHandle, texture, TextureSwizzle, r, TextureSwizzle, g, TextureSwizzle, b, TextureSwizzle, a)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureViewSwizzleAsync, TextureHandle, texture, TextureSwizzle, r, TextureSwizzle, g, TextureSwizzle, b, TextureSwizzle, a, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureExternalImage2, SamplerType, target, TextureFormat, format, uint32_t, width, uint32_t, height, TextureUsage, usage, Platform::ExternalImageHandleRef, image)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureExternalImage, SamplerType, target, TextureFormat, format, uint32_t, width, uint32_t, height, TextureUsage, usage, void*, image)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, CreateTextureExternalImagePlane, TextureFormat, format, uint32_t, width, uint32_t, height, TextureUsage, usage, void*, image, uint32_t, plane)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, ImportTexture, intptr_t, id, SamplerType, target, uint8_t, levels, TextureFormat, format, uint8_t, samples, uint32_t, width, uint32_t, height, uint32_t, depth, TextureUsage, usage)

DECL_DRIVER_API_TAGGED_R_N(TextureHandle, ImportTextureAsync, intptr_t, id, SamplerType, target, uint8_t, levels, TextureFormat, format, uint8_t, samples, uint32_t, width, uint32_t, height, uint32_t, depth, TextureUsage, usage, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_TAGGED_R_N(RenderPrimitiveHandle, CreateRenderPrimitive, VertexBufferHandle, vbh, IndexBufferHandle, ibh, PrimitiveType, pt)

DECL_DRIVER_API_TAGGED_R_N(ProgramHandle, CreateProgram, Program&&, program)

DECL_DRIVER_API_TAGGED_R_N(RenderTargetHandle, CreateDefaultRenderTarget)

DECL_DRIVER_API_TAGGED_R_N(RenderTargetHandle, CreateRenderTarget, TargetBufferFlags, targetBufferFlags, uint32_t, width, uint32_t, height, uint8_t, samples, uint8_t, layerCount, MRT, color, TargetBufferInfo, depth, TargetBufferInfo, stencil)

DECL_DRIVER_API_TAGGED_R_N(FenceHandle, createFence)

DECL_DRIVER_API_TAGGED_R_N(SyncHandle, CreateSync)

DECL_DRIVER_API_TAGGED_R_N(SwapChainHandle, CreateSwapChain, void*, nativeWindow, uint64_t, flags)

DECL_DRIVER_API_TAGGED_R_N(SwapChainHandle, CreateSwapChainHeadless, uint32_t, width, uint32_t, height, uint64_t, flags)

DECL_DRIVER_API_TAGGED_R_N(TimerQueryHandle, CreateTimerQuery)

DECL_DRIVER_API_TAGGED_R_N(DescriptorSetLayoutHandle, CreateDescriptorSetLayout, DescriptorSetLayout&&, info)

DECL_DRIVER_API_TAGGED_R_N(DescriptorSetHandle, CreateDescriptorSet, DescriptorSetLayoutHandle, dslh)

DECL_DRIVER_API_TAGGED_R_N(MemoryMappedBufferHandle, MapBuffer, BufferObjectHandle, boh, size_t, offset, size_t, size, MapBufferAccessFlags, access)

DECL_DRIVER_API_N(DestroyVertexBuffer, VertexBufferHandle, vbh)

DECL_DRIVER_API_N(DestroyVertexBufferInfo, VertexBufferInfoHandle, vbih)

DECL_DRIVER_API_N(DestroyIndexBuffer, IndexBufferHandle, ibh)

DECL_DRIVER_API_N(DestroyBufferObject, BufferObjectHandle, boh)

DECL_DRIVER_API_N(DestroyRenderPrimitive, RenderPrimitiveHandle, rph)

DECL_DRIVER_API_N(DestroyProgram, ProgramHandle, ph)

DECL_DRIVER_API_N(DestroyTexture, TextureHandle, th)

DECL_DRIVER_API_N(DestroyRenderTarget, RenderTargetHandle, rth)

DECL_DRIVER_API_N(DestroySwapChain, SwapChainHandle, sch)

DECL_DRIVER_API_N(DestroyStream, StreamHandle, sh)

DECL_DRIVER_API_N(DestroyTimerQuery, TimerQueryHandle, sh)

DECL_DRIVER_API_N(destroyFence, FenceHandle, fh)

DECL_DRIVER_API_N(DestroySync, SyncHandle, sh)

DECL_DRIVER_API_N(DestroyDescriptorSetLayout, DescriptorSetLayoutHandle, dslh)

DECL_DRIVER_API_N(DestroyDescriptorSet, DescriptorSetHandle, dsh)

DECL_DRIVER_API_N(UnmapBuffer, MemoryMappedBufferHandle, mmbh)

DECL_DRIVER_API_SYNCHRONOUS_0(void, terminate)

DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N(StreamHandle, CreateStreamNative, void*, stream)

DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N(StreamHandle, CreateStreamAcquired)

DECL_DRIVER_API_SYNCHRONOUS_N(void, SetAcquiredImage, StreamHandle, stream, void*, image, const ::math::mat3f&, transform, CallbackHandler*, handler, StreamCallback, cb, void*, userData)

DECL_DRIVER_API_SYNCHRONOUS_N(void, SetStreamDimensions, StreamHandle, stream, uint32_t, width, uint32_t, height)

DECL_DRIVER_API_SYNCHRONOUS_N(int64_t, GetStreamTimestamp, StreamHandle, stream)

DECL_DRIVER_API_SYNCHRONOUS_N(void, UpdateStreams, DriverApi*, driver)

DECL_DRIVER_API_SYNCHRONOUS_N(FenceStatus, GetFenceStatus, FenceHandle, fh)

DECL_DRIVER_API_SYNCHRONOUS_N(FenceStatus, FenceWait, FenceHandle, fh, uint64_t, timeout)

DECL_DRIVER_API_SYNCHRONOUS_N(void, FenceCancel, FenceHandle, fh)

DECL_DRIVER_API_SYNCHRONOUS_N(void, GetPlatformSync, SyncHandle, sh, CallbackHandler*, handler, Platform::SyncCallback, cb, void*, userData)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsTextureFormatSupported, TextureFormat, format)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsTextureSwizzleSupported)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsTextureFormatMipmappable, TextureFormat, format)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsTextureFormatFilterable, TextureFormat, format)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsRenderTargetFormatSupported, TextureFormat, format)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsFrameBufferFetchSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsFrameBufferFetchMultiSampleSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsFrameTimeSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsAutoDepthResolveSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsSRGBSwapChainSupported)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsMSAASwapChainSupported, uint32_t, samples)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsProtectedContentSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsStereoSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsParallelShaderCompileSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsDepthStencilResolveSupported)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsDepthStencilBlitSupported, TextureFormat, format)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsProtectedTexturesSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsDepthClampSupported)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsAsynchronousModeEnabled)

DECL_DRIVER_API_SYNCHRONOUS_0(uint8_t, GetMaxDrawBuffers)

DECL_DRIVER_API_SYNCHRONOUS_0(size_t, GetMaxUniformBufferSize)

DECL_DRIVER_API_SYNCHRONOUS_N(size_t, GetMaxTextureSize, SamplerType, target)

DECL_DRIVER_API_SYNCHRONOUS_0(size_t, GetMaxArrayTextureLayers)

DECL_DRIVER_API_SYNCHRONOUS_0(::math::float2, GetClipSpaceParams)

DECL_DRIVER_API_SYNCHRONOUS_N(void, SetupExternalImage2, Platform::ExternalImageHandleRef, image)

DECL_DRIVER_API_SYNCHRONOUS_N(void, SetupExternalImage, void*, image)

DECL_DRIVER_API_SYNCHRONOUS_N(TimerQueryResult, GetTimerQueryValue, TimerQueryHandle, tqh, uint64_t*, elapsedTime)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, IsWorkaroundNeeded, Workaround, workaround)

DECL_DRIVER_API_SYNCHRONOUS_0(FeatureLevel, GetFeatureLevel)

DECL_DRIVER_API_SYNCHRONOUS_0(size_t, GetUniformBufferOffsetAlignment)

DECL_DRIVER_API_SYNCHRONOUS_0(bool, IsCompositorTimingSupported)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, QueryFrameTimestamps, SwapChainHandle, swapChain, uint64_t, frameId, Platform::FrameTimestamps*, outFrameTimestamps)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, QueryCompositorTiming, SwapChainHandle, swapChain, Platform::CompositorTiming*, outCompositorTiming)

DECL_DRIVER_API_N(SetVertexBufferObject, VertexBufferHandle, vbh, uint32_t, index, BufferObjectHandle, boh)

DECL_DRIVER_API_R_N(AsyncCallId, SetVertexBufferObjectAsync, VertexBufferHandle, vbh, uint32_t, index, BufferObjectHandle, bufferObject, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_N(UpdateIndexBuffer, IndexBufferHandle, ibh, BufferDescriptor&&, data, uint32_t, byteOffset)

DECL_DRIVER_API_R_N(AsyncCallId, UpdateIndexBufferAsync, IndexBufferHandle, ibh, BufferDescriptor&&, data, uint32_t, byteOffset, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_N(UpdateBufferObject, BufferObjectHandle, ibh, BufferDescriptor&&, data, uint32_t, byteOffset)

DECL_DRIVER_API_R_N(AsyncCallId, UpdateBufferObjectAsync, BufferObjectHandle, ibh, BufferDescriptor&&, data, uint32_t, byteOffset, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_N(UpdateBufferObjectUnsynchronized, BufferObjectHandle, boh, BufferDescriptor&&, data, uint32_t, byteOffset)

DECL_DRIVER_API_N(ResetBufferObject, BufferObjectHandle, boh)

DECL_DRIVER_API_N(Update3DImage, TextureHandle, th, uint32_t, level, uint32_t, xoffset, uint32_t, yoffset, uint32_t, zoffset, uint32_t, width, uint32_t, height, uint32_t, depth, PixelBufferDescriptor&&, data)

DECL_DRIVER_API_R_N(AsyncCallId, Update3DImageAsync, TextureHandle, th, uint32_t, level, uint32_t, xoffset, uint32_t, yoffset, uint32_t, zoffset, uint32_t, width, uint32_t, height, uint32_t, depth, PixelBufferDescriptor&&, data, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_N(GenerateMipmaps, TextureHandle, th)

DECL_DRIVER_API_N(SetExternalStream, TextureHandle, th, StreamHandle, sh)

DECL_DRIVER_API_N(BeginTimerQuery, TimerQueryHandle, tqh)

DECL_DRIVER_API_N(EndTimerQuery, TimerQueryHandle, tqh)

DECL_DRIVER_API_N(CompilePrograms, CompilerPriorityQueue, priority, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_N(UpdateDescriptorSetBuffer, DescriptorSetHandle, dsh, descriptor_binding_t, binding, BufferObjectHandle, boh, uint32_t, offset, uint32_t, size)

DECL_DRIVER_API_N(UpdateDescriptorSetTexture, DescriptorSetHandle, dsh, descriptor_binding_t, binding, TextureHandle, th, SamplerParams, params)

DECL_DRIVER_API_N(CopyToMemoryMappedBuffer, MemoryMappedBufferHandle, mmbh, size_t, offset, BufferDescriptor&&, data)

DECL_DRIVER_API_N(SetPushConstant, ShaderStage, stage, uint8_t, index, PushConstantVariant, value)

DECL_DRIVER_API_N(BeginRenderPass, RenderTargetHandle, rth, const RenderPassParams&, params)

DECL_DRIVER_API_0(EndRenderPass)

DECL_DRIVER_API_0(NextSubpass)

DECL_DRIVER_API_N(BindPipeline, PipelineState const&, state)

DECL_DRIVER_API_N(BindRenderPrimitive, RenderPrimitiveHandle, rph)

DECL_DRIVER_API_N(BindDescriptorSet, DescriptorSetHandle, dsh, descriptor_set_t, set, DescriptorSetOffsetArray&&, offsets)

DECL_DRIVER_API_N(MakeCurrent, SwapChainHandle, schDraw, SwapChainHandle, schRead)

DECL_DRIVER_API_N(Commit, SwapChainHandle, sch)

DECL_DRIVER_API_N(InsertEventMarker, const char*, string)

DECL_DRIVER_API_N(PushGroupMarker, const char*, string)

DECL_DRIVER_API_0(PopGroupMarker)

DECL_DRIVER_API_0(StartCapture)

DECL_DRIVER_API_0(StopCapture)

DECL_DRIVER_API_N(ReadPixels, RenderTargetHandle, src, uint32_t, x, uint32_t, y, uint32_t, width, uint32_t, height, PixelBufferDescriptor&&, data)

DECL_DRIVER_API_N(ReadTexture, TextureHandle, src, uint8_t, level, uint16_t, layer, uint32_t, x, uint32_t, y, uint32_t, width, uint32_t, height, PixelBufferDescriptor&&, data)

DECL_DRIVER_API_N(ReadBufferSubData, BufferObjectHandle, src, uint32_t, offset, uint32_t, size, BufferDescriptor&&, data)

DECL_DRIVER_API_N(BlitDEPRECATED, TargetBufferFlags, buffers, RenderTargetHandle, dst, Viewport, dstRect, RenderTargetHandle, src, Viewport, srcRect, SamplerMagFilter, filter)

DECL_DRIVER_API_N(Resolve, TextureHandle, dst, uint8_t, dstLevel, uint8_t, dstLayer, TextureHandle, src, uint8_t, srcLevel, uint8_t, srcLayer)

DECL_DRIVER_API_N(Blit, TextureHandle, dst, uint8_t, dstLevel, uint8_t, dstLayer, ::math::uint2, dstOrigin, TextureHandle, src, uint8_t, srcLevel, uint8_t, srcLayer, ::math::uint2, srcOrigin, ::math::uint2, size)

DECL_DRIVER_API_N(Draw2, uint32_t, indexOffset, uint32_t, indexCount, uint32_t, instanceCount)

DECL_DRIVER_API_N(DrawArrays, uint32_t, vertexOffset, uint32_t, vertexCount, uint32_t, instanceCount)

DECL_DRIVER_API_N(Draw, PipelineState, state, RenderPrimitiveHandle, rph, uint32_t, indexOffset, uint32_t, indexCount, uint32_t, instanceCount)

DECL_DRIVER_API_N(DispatchCompute, ProgramHandle, program, ::math::uint3, workGroupCount)

DECL_DRIVER_API_N(Scissor, Viewport, scissor)

DECL_DRIVER_API_R_N(AsyncCallId, QueueCommandAsync, std::function<void()>&&, command, CallbackHandler*, handler, CallbackHandler::Callback, callback, void*, user)

DECL_DRIVER_API_SYNCHRONOUS_N(bool, CancelAsyncJob, AsyncCallId, jobId)

#import <Metal/Metal.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../fsr4/ffx_frame.hpp"
#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#define LOAD(level, name) auto name = reinterpret_cast<PFN_##name>(level(#name)); if (!name) { fprintf(stderr, "%s unavailable\n", #name); return 1; }
#define VK(expr) do { const VkResult result = (expr); if (result != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #expr, result); return 1; } } while (0)
int main(int argc, char **argv)
{
 @autoreleasepool {
    if (argc != 3) return 2;
    void *icd = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
    if (!icd) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    auto gpa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(icd, "vk_icdGetInstanceProcAddr"));
    if (!gpa) return 2;
    auto global = [&](const char *name){ return gpa(nullptr, name); };
    LOAD(global, vkCreateInstance)
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance = nullptr;
    VK(vkCreateInstance(&ici, nullptr, &instance));
    auto inst = [&](const char *name){ return gpa(instance, name); };
    LOAD(inst, vkDestroyInstance)
    LOAD(inst, vkEnumeratePhysicalDevices)
    LOAD(inst, vkGetPhysicalDeviceQueueFamilyProperties)
    LOAD(inst, vkGetPhysicalDeviceMemoryProperties)
    LOAD(inst, vkGetPhysicalDeviceImageFormatProperties2)
    LOAD(inst, vkCreateDevice)
    LOAD(inst, vkGetDeviceProcAddr)
    uint32_t n = 1;
    VkPhysicalDevice physical;
    VK(vkEnumeratePhysicalDevices(instance, &n, &physical));
    if (n != 1) return 1;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    if (!n || n > 32) return 1;
    VkQueueFamilyProperties families[32];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families);
    uint32_t family = 0;
    while (family < n && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == n) return 1;
    constexpr auto handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkPhysicalDeviceExternalImageFormatInfo external = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    external.handleType = handleType;
    VkPhysicalDeviceImageFormatInfo2 query = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, &external};
    query.type = VK_IMAGE_TYPE_2D;
    query.tiling = VK_IMAGE_TILING_OPTIMAL;
    query.usage = usage;
    VkExternalImageFormatProperties externalProps = {VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 formatProps = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &externalProps};
    for (auto format : {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16_SFLOAT}) {
        query.format = format;
        VK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &formatProps));
        auto props = externalProps.externalMemoryProperties;
        printf("format=%u import=%d export=%d dedicated=%d\n", format,
            !!(props.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT),
            !!(props.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT),
            !!(props.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT));
        if (!(props.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) return 1;
    }
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    const char *ext = VK_EXT_EXTERNAL_MEMORY_METAL_EXTENSION_NAME;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timelineFeature.timelineSemaphore = VK_TRUE;
    dci.pNext = &timelineFeature;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = &ext;
    VkDevice device;
    VK(vkCreateDevice(physical, &dci, nullptr, &device));
    auto dev = [&](const char *name){ return vkGetDeviceProcAddr(device, name); };
    LOAD(dev, vkDestroyDevice)
    LOAD(dev, vkGetMemoryMetalHandleEXT)
    LOAD(dev, vkCreateImage)
    LOAD(dev, vkGetImageMemoryRequirements)
    LOAD(dev, vkAllocateMemory)
    LOAD(dev, vkBindImageMemory)
    LOAD(dev, vkGetDeviceQueue)
    LOAD(dev, vkCreateCommandPool)
    LOAD(dev, vkAllocateCommandBuffers)
    LOAD(dev, vkBeginCommandBuffer)
    LOAD(dev, vkCmdPipelineBarrier)
    LOAD(dev, vkCmdClearColorImage)
    LOAD(dev, vkEndCommandBuffer)
    LOAD(dev, vkQueueSubmit)
    LOAD(dev, vkCreateSemaphore)
    LOAD(dev, vkDestroySemaphore)
    LOAD(dev, vkSignalSemaphore)
    LOAD(dev, vkWaitSemaphores)
    LOAD(dev, vkDestroyCommandPool)
    LOAD(dev, vkCreateBuffer)
    LOAD(dev, vkGetBufferMemoryRequirements)
    LOAD(dev, vkBindBufferMemory)
    LOAD(dev, vkCmdCopyImageToBuffer)
    LOAD(dev, vkMapMemory)
    LOAD(dev, vkUnmapMemory)
    LOAD(dev, vkDestroyBuffer)
    LOAD(dev, vkDestroyImage)
    LOAD(dev, vkFreeMemory)
    // The probe controls allocation and format. An arbitrary game image
    // may have a different heap offset, layout, or texture usage.
    struct Surface { VkImage image; VkDeviceMemory memory; id<MTLTexture> texture; };
    constexpr uint32_t rw = 2314, rh = 1510, uw = 3472, uh = 2266;
    const uint32_t widths[] = {rw, rw, rw, uw}, heights[] = {rh, rh, rh, uh};
    const VkFormat formats[] = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT};
    const MTLPixelFormat metalFormats[] = {MTLPixelFormatRGBA16Float, MTLPixelFormatR32Float,
        MTLPixelFormatRG16Float, MTLPixelFormatRGBA16Float};
    Surface surfaces[4] = {};
    id<MTLDevice> metal = nil;
    for (unsigned i = 0; i < 4; ++i) {
        Surface &s = surfaces[i];
        VkExternalMemoryImageCreateInfo eici = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        eici.handleTypes = handleType;
        VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &eici};
        imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = formats[i];
        imageInfo.extent = {widths[i], heights[i], 1}; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        VK(vkCreateImage(device, &imageInfo, nullptr, &s.image));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, s.image, &req);
        if (!req.memoryTypeBits) return 1;
        VkExportMemoryAllocateInfo exportInfo = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        exportInfo.handleTypes = handleType;
        VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exportInfo};
        allocation.allocationSize = req.size; allocation.memoryTypeIndex = __builtin_ctz(req.memoryTypeBits);
        VK(vkAllocateMemory(device, &allocation, nullptr, &s.memory));
        VK(vkBindImageMemory(device, s.image, s.memory, 0));
        VkMemoryGetMetalHandleInfoEXT handleInfo = {VK_STRUCTURE_TYPE_MEMORY_GET_METAL_HANDLE_INFO_EXT};
        handleInfo.memory = s.memory; handleInfo.handleType = handleType;
        void *handle = nullptr;
        VK(vkGetMemoryMetalHandleEXT(device, &handleInfo, &handle));
        id<MTLHeap> heap = (__bridge id<MTLHeap>)handle;
        if (!heap || (metal && metal != heap.device)) return 1;
        metal = heap.device;
        auto *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalFormats[i]
            width:widths[i] height:heights[i] mipmapped:NO];
        desc.storageMode = heap.storageMode; desc.cpuCacheMode = heap.cpuCacheMode;
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
        s.texture = [heap newTextureWithDescriptor:desc offset:0];
        if (!s.texture) return 1;
    }
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = family;
    VkCommandPool pool;
    VK(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
    VkCommandBufferAllocateInfo commandInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = pool; commandInfo.commandBufferCount = 2;
    VkCommandBuffer commands[2];
    VK(vkAllocateCommandBuffers(device, &commandInfo, commands));
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(commands[0], &begin));
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkClearColorValue colors[4] = {{{0.25f, 0.5f, 0.75f, 1.f}}, {{0.5f}}, {{0.f}}, {{0.f}}};
    for (unsigned i = 0; i < 4; ++i) {
        barrier.image = surfaces[i].image;
        vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkCmdClearColorImage(commands[0], barrier.image, barrier.newLayout, &colors[i], 1, &barrier.subresourceRange);
    }
    VK(vkEndCommandBuffer(commands[0]));
    VkSemaphoreTypeCreateInfo semType = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    semType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &semType};
    VkSemaphore timeline;
    VK(vkCreateSemaphore(device, &semInfo, nullptr, &timeline));
    constexpr uint64_t inputReady = 1, fsrReady = 2, readbackReady = 3;
    VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.signalSemaphoreValueCount = 1; values.pSignalSemaphoreValues = &inputReady;
    VkSubmitInfo submission = {VK_STRUCTURE_TYPE_SUBMIT_INFO, &values};
    submission.commandBufferCount = 1; submission.pCommandBuffers = &commands[0];
    submission.signalSemaphoreCount = 1; submission.pSignalSemaphores = &timeline;
    VK(vkQueueSubmit(queue, 1, &submission, nullptr));
    const auto wait = [&](const uint64_t &value) {
        VkSemaphoreWaitInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        info.semaphoreCount = 1; info.pSemaphores = &timeline; info.pValues = &value;
        return vkWaitSemaphores(device, &info, 20000000000ull);
    };

    auto fsr = dlopen(argv[2], RTLD_LOCAL | RTLD_NOW);
    if (!fsr) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    auto encode = reinterpret_cast<decltype(&fsr4_encode_live_frame_v2)>(dlsym(fsr, "fsr4_encode_live_frame_v2"));
    auto release = reinterpret_cast<decltype(&fsr4_release_live_owner)>(dlsym(fsr, "fsr4_release_live_owner"));
    auto status = reinterpret_cast<decltype(&fsr4_live_get_status)>(dlsym(fsr, "fsr4_live_get_status"));
    if (!encode || !release || !status) return 1;
    const ffxCreateContextDescUpscale context = {{FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE},
        FFX_UPSCALE_ENABLE_AUTO_EXPOSURE, {rw, rh}, {uw, uh}, nullptr};
    ffxDispatchDescUpscale dispatch = {{FFX_API_DISPATCH_DESC_TYPE_UPSCALE}};
    dispatch.commandList = &commands[0];
    dispatch.color.resource = &surfaces[0];
    dispatch.depth.resource = &surfaces[1];
    dispatch.motionVectors.resource = &surfaces[2];
    dispatch.output.resource = &surfaces[3];
    dispatch.renderSize = {rw, rh};
    dispatch.motionVectorScale = {-1.f, 1.f};
    dispatch.preExposure = 1.f;
    dispatch.reset = true;
    const auto surface = [&](unsigned i) -> fh6::MetalSurface {
        return {(__bridge void *)surfaces[i].texture, widths[i], heights[i]};
    };
    Fsr4LiveFrameV2 args = {};
    fh6::MetalUpscaleInputs inputs = {(__bridge void *)metal, nullptr, &args,
        surface(0), surface(1), surface(2), {}, surface(3)};
    id<MTLCommandQueue> mq = [metal newCommandQueue];
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = uint64_t(uw) * uh * 8; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;
    VK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    unsigned type = 0;
    constexpr auto hostFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < props.memoryTypeCount && (!(req.memoryTypeBits & (1u << type)) ||
        (props.memoryTypes[type].propertyFlags & hostFlags) != hostFlags)) ++type;
    if (type == props.memoryTypeCount) return 1;
    VkMemoryAllocateInfo bufferAlloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bufferAlloc.allocationSize = req.size; bufferAlloc.memoryTypeIndex = type;
    VkDeviceMemory bufferMemory;
    VK(vkAllocateMemory(device, &bufferAlloc, nullptr, &bufferMemory));
    VK(vkBindBufferMemory(device, buffer, bufferMemory, 0));
    VK(vkBeginCommandBuffer(commands[1], &begin));
    barrier.image = surfaces[3].image; barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commands[1], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uw, uh, 1};
    vkCmdCopyImageToBuffer(commands[1], barrier.image, barrier.newLayout, buffer, 1, &copy);
    VK(vkEndCommandBuffer(commands[1]));
    VK(wait(inputReady));
    for (unsigned i = 0; i < 6; ++i) {
        id<MTLCommandBuffer> cb = [mq commandBuffer];
        inputs.commandBuffer = (__bridge void *)cb;
        if (i == 0) {
            auto invalid = dispatch;
            invalid.reactive.resource = &surfaces[0];
            if (fh6::prepareFsr4Frame(context, invalid, inputs, args) || args.structSize)
                return 1;
            invalid = dispatch;
            invalid.upscaleSize = {uw + 1, uh};
            if (fh6::prepareFsr4Frame(context, invalid, inputs, args) || args.structSize)
                return 1;
        }
        if (!fh6::prepareFsr4Frame(context, dispatch, inputs, args) ||
            args.frame.mvScaleX != -1.f || args.frame.uw != uw || args.frame.uh != uh)
            return 1;
        if (!encode(&args)) {
            Fsr4LiveStatus state = {};
            status(&state);
            fprintf(stderr, "native FSR encode failed: %s\n", state.reason);
            return 1;
        }
        if (i == 5) {
            // Submit the Vulkan consumer before Metal completes. The
            // callback only releases the consumer after a successful encode.
            values.waitSemaphoreValueCount = 1; values.pWaitSemaphoreValues = &fsrReady;
            values.pSignalSemaphoreValues = &readbackReady;
            VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            submission.waitSemaphoreCount = 1; submission.pWaitSemaphores = &timeline;
            submission.pWaitDstStageMask = &stage; submission.pCommandBuffers = &commands[1];
            VK(vkQueueSubmit(queue, 1, &submission, nullptr));
            [cb addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if (completed.status != MTLCommandBufferStatusCompleted) return;
                VkSemaphoreSignalInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
                signal.semaphore = timeline; signal.value = fsrReady;
                auto result = vkSignalSemaphore(device, &signal);
                if (result) fprintf(stderr, "vkSignalSemaphore: %d\n", result);
            }];
        }
        [cb commit];
        if (i == 5) VK(wait(readbackReady));
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) {
            NSLog(@"%@", cb.error); return 1;
        }
        printf("Native Metal FSR frame %u: %.3f ms GPU\n", i, (cb.GPUEndTime - cb.GPUStartTime) * 1000);
        dispatch.reset = false;
    }
    Fsr4LiveStatus state = {};
    status(&state);
    printf("FSR backend=%s variant=%s replaced=%llu\n", state.model, state.variant, (unsigned long long)state.replaced);
    release(inputs.device, inputs.owner);

    void *mapped;
    VK(vkMapMemory(device, bufferMemory, 0, bufferInfo.size, 0, &mapped));
    const auto *pixels = static_cast<const uint16_t *>(mapped);
    unsigned correct = 0, total = 0;
    for (unsigned y = 8; y < uh - 8; y += 32)
        for (unsigned x = 8; x < uw - 8; x += 32) {
            auto p = pixels + (y * uw + x) * 4;
            correct += p[0] > 0 && p[0] < p[1] && p[1] < p[2] && p[2] < 0x7c00;
            ++total;
        }
    const auto *mid = pixels + ((uh/2) * uw + uw/2) * 4;
    printf("Vulkan -> native Metal FSR -> Vulkan: %u/%u ordered RGB pixels, center=%04x,%04x,%04x\n",
        correct, total, mid[0], mid[1], mid[2]);
    vkUnmapMemory(device, bufferMemory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, bufferMemory, nullptr);
    vkDestroySemaphore(device, timeline, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    for (auto &s : surfaces) {
        s.texture = nil;
        vkDestroyImage(device, s.image, nullptr);
        vkFreeMemory(device, s.memory, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return correct != total || state.replaced != 6;
 }
}

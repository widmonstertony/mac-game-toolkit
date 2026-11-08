#include "config.h"
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/vulkan_driver.h"
#include "wine/unixlib.h"
#include "wine_heap.h"

NTSTATUS fh6_export_heap(void *data)
{
    struct metal_heap_request *request = data;
    VkMemoryGetMetalHandleInfoEXT info = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_METAL_HANDLE_INFO_EXT};
    void *heap = NULL;

    if (!request || !request->device || !request->memory) return STATUS_INVALID_PARAMETER;
    request->heap = 0;
    struct vulkan_device *device = vulkan_device_from_handle((VkDevice)(UINT_PTR)request->device);
    struct vulkan_device_memory *memory = vulkan_device_memory_from_handle((VkDeviceMemory)request->memory);
    if (!device || !device->p_vkGetMemoryMetalHandleEXT) return STATUS_NOT_SUPPORTED;
    info.memory = memory->host.device_memory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
    if (device->p_vkGetMemoryMetalHandleEXT(device->host.device, &info, &heap) != VK_SUCCESS || !heap)
        return STATUS_NOT_SUPPORTED;
    request->heap = (UINT_PTR)heap;
    return 0;
}

extern NTSTATUS fh6_encode_fsr(void *);
extern NTSTATUS fh6_validate_fsr(void *);

const unixlib_entry_t __wine_unix_call_funcs[] = {fh6_encode_fsr, fh6_validate_fsr};

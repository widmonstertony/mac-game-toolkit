#pragma once

#include <stdint.h>

struct metal_heap_request {
    uint64_t device;
    uint64_t memory;
    uint64_t heap;
};

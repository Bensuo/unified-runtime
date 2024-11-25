// Copyright (C) 2024 Intel Corporation
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM Exceptions.
// See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <sycl/sycl.hpp>

int main() {
    size_t array_size = 16;
    size_t local_size = 4;
    sycl::queue sycl_queue;
    uint32_t *X = sycl::malloc_shared<uint32_t>(array_size, sycl_queue);
    uint32_t *Y = sycl::malloc_shared<uint32_t>(array_size, sycl_queue);
    uint32_t *Z = sycl::malloc_shared<uint32_t>(array_size, sycl_queue);
    uint32_t A = 42;

    sycl_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor<uint32_t, 1> local_mem_A(local_size, cgh);
        sycl::local_accessor<uint32_t, 1> local_mem_B(local_size * 2, cgh);
        cgh.parallel_for(
            sycl::nd_range<1>{{array_size}, {local_size}},
            [=](sycl::nd_item<1> itemId) {
               /* Kernel Code */
            });
    });


    __global__ void kernel(unsigned OffsetA, unsigned OffsetB) {
      extern __shared__ int LocalMem[];
      int *OffsetLocalPtr1 = &LocalMem[Offset1];
      int *OffsetLocalPtr2 = &LocalMem[Offset2];
      /* Kernel Code */
    }


}




    return 0;
}

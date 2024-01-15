//===--------- enqueue.hpp - HIP Adapter ---------------------------------===//
//
// Copyright (C) 2024 Intel Corporation
//
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM
// Exceptions. See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cassert>
#include <hip/hip_runtime.h>
#include <ur_api.h>

ur_result_t enqueueEventsWait(ur_queue_handle_t CommandQueue,
                              hipStream_t Stream, uint32_t NumEventsInWaitList,
                              const ur_event_handle_t *EventWaitList);

void guessLocalWorkSize(ur_device_handle_t Device, size_t *ThreadsPerBlock,
                        const size_t *GlobalWorkSize, const uint32_t WorkDim,
                        const size_t MaxThreadsPerBlock[3],
                        ur_kernel_handle_t Kernel, uint32_t LocalSize);

ur_result_t
setKernelParams(const ur_device_handle_t Device, const uint32_t WorkDim,
                const size_t *GlobalWorkOffset, const size_t *GlobalWorkSize,
                const size_t *LocalWorkSize, ur_kernel_handle_t &Kernel,
                hipFunction_t &HIPFunc, size_t (&ThreadsPerBlock)[3],
                size_t (&BlocksPerGrid)[3]);

void setCopyRectParams(ur_rect_region_t region, const void *SrcPtr,
                       const hipMemoryType SrcType, ur_rect_offset_t src_offset,
                       size_t src_row_pitch, size_t src_slice_pitch,
                       void *DstPtr, const hipMemoryType DstType,
                       ur_rect_offset_t dst_offset, size_t dst_row_pitch,
                       size_t dst_slice_pitch, hipMemcpy3DParms &params);
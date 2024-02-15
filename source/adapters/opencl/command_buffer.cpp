//===--------- command_buffer.cpp - OpenCL Adapter ---------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM
// Exceptions. See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "command_buffer.hpp"
#include "common.hpp"

namespace {
ur_result_t
commandBufferReleaseInternal(ur_exp_command_buffer_handle_t CommandBuffer) {
  if (CommandBuffer->decrementInternalReferenceCount() != 0) {
    return UR_RESULT_SUCCESS;
  }

  delete CommandBuffer;
  return UR_RESULT_SUCCESS;
}

ur_result_t
commandHandleReleaseInternal(ur_exp_command_buffer_command_handle_t Command) {
  if (Command->decrementInternalReferenceCount() != 0) {
    return UR_RESULT_SUCCESS;
  }

  // Decrement parent command-buffer internal ref count
  commandBufferReleaseInternal(Command->hCommandBuffer);

  delete Command;
  return UR_RESULT_SUCCESS;
}
} // end anonymous namespace

/// The ur_exp_command_buffer_handle_t_ destructor calls CL release
/// command-buffer to free the underlying object.
ur_exp_command_buffer_handle_t_::~ur_exp_command_buffer_handle_t_() {
  urQueueRelease(hInternalQueue);

  cl_context CLContext = cl_adapter::cast<cl_context>(hContext);
  cl_ext::clReleaseCommandBufferKHR_fn clReleaseCommandBufferKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clReleaseCommandBufferKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clReleaseCommandBufferKHRCache,
          cl_ext::ReleaseCommandBufferName, &clReleaseCommandBufferKHR);
  assert(Res == CL_SUCCESS);

  clReleaseCommandBufferKHR(CLCommandBuffer);
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferCreateExp(
    ur_context_handle_t hContext, ur_device_handle_t hDevice,
    const ur_exp_command_buffer_desc_t *pCommandBufferDesc,
    ur_exp_command_buffer_handle_t *phCommandBuffer) {

  ur_queue_handle_t Queue = nullptr;
  UR_RETURN_ON_FAILURE(urQueueCreate(hContext, hDevice, nullptr, &Queue));

  cl_context CLContext = cl_adapter::cast<cl_context>(hContext);
  cl_ext::clCreateCommandBufferKHR_fn clCreateCommandBufferKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clCreateCommandBufferKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clCreateCommandBufferKHRCache,
          cl_ext::CreateCommandBufferName, &clCreateCommandBufferKHR);

  if (!clCreateCommandBufferKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  const bool IsUpdatable =
      pCommandBufferDesc ? pCommandBufferDesc->isUpdatable : false;

  bool DeviceSupportsUpdate = false;
  cl_device_id CLDevice = cl_adapter::cast<cl_device_id>(hDevice);
  CL_RETURN_ON_FAILURE(deviceSupportsURCommandBufferKernelUpdate(
      CLDevice, DeviceSupportsUpdate));

  if (IsUpdatable && !DeviceSupportsUpdate) {
    return UR_RESULT_ERROR_INVALID_OPERATION;
  }

  cl_command_buffer_properties_khr Properties[3] = {
      CL_COMMAND_BUFFER_FLAGS_KHR,
      IsUpdatable ? CL_COMMAND_BUFFER_MUTABLE_KHR : 0u, 0};
  auto CLCommandBuffer = clCreateCommandBufferKHR(
      1, cl_adapter::cast<cl_command_queue *>(&Queue), Properties, &Res);
  CL_RETURN_ON_FAILURE_AND_SET_NULL(Res, phCommandBuffer);

  try {
    auto URCommandBuffer = std::make_unique<ur_exp_command_buffer_handle_t_>(
        Queue, hContext, CLCommandBuffer, IsUpdatable);
    *phCommandBuffer = URCommandBuffer.release();
  } catch (...) {
    return UR_RESULT_ERROR_OUT_OF_RESOURCES;
  }

  CL_RETURN_ON_FAILURE(Res);
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL
urCommandBufferRetainExp(ur_exp_command_buffer_handle_t hCommandBuffer) {
  hCommandBuffer->incrementInternalReferenceCount();
  hCommandBuffer->incrementExternalReferenceCount();
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL
urCommandBufferReleaseExp(ur_exp_command_buffer_handle_t hCommandBuffer) {
  if (hCommandBuffer->decrementExternalReferenceCount() == 0) {
    // External ref count has reached zero, internal release of created
    // commands.
    for (auto Command : hCommandBuffer->CommandHandles) {
      commandHandleReleaseInternal(Command);
    }
  }

  return commandBufferReleaseInternal(hCommandBuffer);
}

UR_APIEXPORT ur_result_t UR_APICALL
urCommandBufferFinalizeExp(ur_exp_command_buffer_handle_t hCommandBuffer) {
  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clFinalizeCommandBufferKHR_fn clFinalizeCommandBufferKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clFinalizeCommandBufferKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clFinalizeCommandBufferKHRCache,
          cl_ext::FinalizeCommandBufferName, &clFinalizeCommandBufferKHR);

  if (!clFinalizeCommandBufferKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  CL_RETURN_ON_FAILURE(
      clFinalizeCommandBufferKHR(hCommandBuffer->CLCommandBuffer));
  hCommandBuffer->IsFinalized = true;
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendKernelLaunchExp(
    ur_exp_command_buffer_handle_t hCommandBuffer, ur_kernel_handle_t hKernel,
    uint32_t workDim, const size_t *pGlobalWorkOffset,
    const size_t *pGlobalWorkSize, const size_t *pLocalWorkSize,
    uint32_t numSyncPointsInWaitList,
    const ur_exp_command_buffer_sync_point_t *pSyncPointWaitList,
    ur_exp_command_buffer_sync_point_t *pSyncPoint,
    ur_exp_command_buffer_command_handle_t *phCommandHandle) {

  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clCommandNDRangeKernelKHR_fn clCommandNDRangeKernelKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clCommandNDRangeKernelKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clCommandNDRangeKernelKHRCache,
          cl_ext::CommandNRRangeKernelName, &clCommandNDRangeKernelKHR);

  if (!clCommandNDRangeKernelKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  cl_mutable_command_khr CommandHandle = nullptr;
  cl_mutable_command_khr *OutCommandHandle =
      hCommandBuffer->IsUpdatable ? &CommandHandle : nullptr;

  cl_ndrange_kernel_command_properties_khr UpdateProperties[] = {
      CL_MUTABLE_DISPATCH_UPDATABLE_FIELDS_KHR,
      CL_MUTABLE_DISPATCH_GLOBAL_OFFSET_KHR |
          CL_MUTABLE_DISPATCH_GLOBAL_SIZE_KHR |
          CL_MUTABLE_DISPATCH_LOCAL_SIZE_KHR |
          CL_MUTABLE_DISPATCH_ARGUMENTS_KHR | CL_MUTABLE_DISPATCH_EXEC_INFO_KHR,
      0};

  cl_ndrange_kernel_command_properties_khr *Properties =
      hCommandBuffer->IsUpdatable ? UpdateProperties : nullptr;
  CL_RETURN_ON_FAILURE(clCommandNDRangeKernelKHR(
      hCommandBuffer->CLCommandBuffer, nullptr, Properties,
      cl_adapter::cast<cl_kernel>(hKernel), workDim, pGlobalWorkOffset,
      pGlobalWorkSize, pLocalWorkSize, numSyncPointsInWaitList,
      pSyncPointWaitList, pSyncPoint, OutCommandHandle));

  try {
    auto URCommandHandle =
        std::make_unique<ur_exp_command_buffer_command_handle_t_>(
            hCommandBuffer, CommandHandle, workDim);
    *phCommandHandle = URCommandHandle.release();
    hCommandBuffer->CommandHandles.push_back(*phCommandHandle);
  } catch (...) {
    return UR_RESULT_ERROR_OUT_OF_RESOURCES;
  }

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendUSMMemcpyExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] void *pDst, [[maybe_unused]] const void *pSrc,
    [[maybe_unused]] size_t size,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendUSMFillExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] void *pMemory, [[maybe_unused]] const void *pPattern,
    [[maybe_unused]] size_t patternSize, [[maybe_unused]] size_t size,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendMemBufferCopyExp(
    ur_exp_command_buffer_handle_t hCommandBuffer, ur_mem_handle_t hSrcMem,
    ur_mem_handle_t hDstMem, size_t srcOffset, size_t dstOffset, size_t size,
    uint32_t numSyncPointsInWaitList,
    const ur_exp_command_buffer_sync_point_t *pSyncPointWaitList,
    ur_exp_command_buffer_sync_point_t *pSyncPoint) {

  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clCommandCopyBufferKHR_fn clCommandCopyBufferKHR = nullptr;
  cl_int Res = cl_ext::getExtFuncFromContext<decltype(clCommandCopyBufferKHR)>(
      CLContext, cl_ext::ExtFuncPtrCache->clCommandCopyBufferKHRCache,
      cl_ext::CommandCopyBufferName, &clCommandCopyBufferKHR);

  if (!clCommandCopyBufferKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  CL_RETURN_ON_FAILURE(clCommandCopyBufferKHR(
      hCommandBuffer->CLCommandBuffer, nullptr,
      cl_adapter::cast<cl_mem>(hSrcMem), cl_adapter::cast<cl_mem>(hDstMem),
      srcOffset, dstOffset, size, numSyncPointsInWaitList, pSyncPointWaitList,
      pSyncPoint, nullptr));

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendMemBufferCopyRectExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] ur_mem_handle_t hSrcMem,
    [[maybe_unused]] ur_mem_handle_t hDstMem,
    [[maybe_unused]] ur_rect_offset_t srcOrigin,
    [[maybe_unused]] ur_rect_offset_t dstOrigin,
    [[maybe_unused]] ur_rect_region_t region,
    [[maybe_unused]] size_t srcRowPitch, [[maybe_unused]] size_t srcSlicePitch,
    [[maybe_unused]] size_t dstRowPitch, [[maybe_unused]] size_t dstSlicePitch,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {

  size_t OpenCLOriginRect[3]{srcOrigin.x, srcOrigin.y, srcOrigin.z};
  size_t OpenCLDstRect[3]{dstOrigin.x, dstOrigin.y, dstOrigin.z};
  size_t OpenCLRegion[3]{region.width, region.height, region.depth};

  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clCommandCopyBufferRectKHR_fn clCommandCopyBufferRectKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clCommandCopyBufferRectKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clCommandCopyBufferRectKHRCache,
          cl_ext::CommandCopyBufferRectName, &clCommandCopyBufferRectKHR);

  if (!clCommandCopyBufferRectKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  CL_RETURN_ON_FAILURE(clCommandCopyBufferRectKHR(
      hCommandBuffer->CLCommandBuffer, nullptr,
      cl_adapter::cast<cl_mem>(hSrcMem), cl_adapter::cast<cl_mem>(hDstMem),
      OpenCLOriginRect, OpenCLDstRect, OpenCLRegion, srcRowPitch, srcSlicePitch,
      dstRowPitch, dstSlicePitch, numSyncPointsInWaitList, pSyncPointWaitList,
      pSyncPoint, nullptr));

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT
ur_result_t UR_APICALL urCommandBufferAppendMemBufferWriteExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] ur_mem_handle_t hBuffer, [[maybe_unused]] size_t offset,
    [[maybe_unused]] size_t size, [[maybe_unused]] const void *pSrc,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {

  cl_adapter::die("Experimental Command-buffer feature is not "
                  "implemented for OpenCL adapter.");
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT
ur_result_t UR_APICALL urCommandBufferAppendMemBufferReadExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] ur_mem_handle_t hBuffer, [[maybe_unused]] size_t offset,
    [[maybe_unused]] size_t size, [[maybe_unused]] void *pDst,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT
ur_result_t UR_APICALL urCommandBufferAppendMemBufferWriteRectExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] ur_mem_handle_t hBuffer,
    [[maybe_unused]] ur_rect_offset_t bufferOffset,
    [[maybe_unused]] ur_rect_offset_t hostOffset,
    [[maybe_unused]] ur_rect_region_t region,
    [[maybe_unused]] size_t bufferRowPitch,
    [[maybe_unused]] size_t bufferSlicePitch,
    [[maybe_unused]] size_t hostRowPitch,
    [[maybe_unused]] size_t hostSlicePitch, [[maybe_unused]] void *pSrc,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT
ur_result_t UR_APICALL urCommandBufferAppendMemBufferReadRectExp(
    [[maybe_unused]] ur_exp_command_buffer_handle_t hCommandBuffer,
    [[maybe_unused]] ur_mem_handle_t hBuffer,
    [[maybe_unused]] ur_rect_offset_t bufferOffset,
    [[maybe_unused]] ur_rect_offset_t hostOffset,
    [[maybe_unused]] ur_rect_region_t region,
    [[maybe_unused]] size_t bufferRowPitch,
    [[maybe_unused]] size_t bufferSlicePitch,
    [[maybe_unused]] size_t hostRowPitch,
    [[maybe_unused]] size_t hostSlicePitch, [[maybe_unused]] void *pDst,
    [[maybe_unused]] uint32_t numSyncPointsInWaitList,
    [[maybe_unused]] const ur_exp_command_buffer_sync_point_t
        *pSyncPointWaitList,
    [[maybe_unused]] ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendMemBufferFillExp(
    ur_exp_command_buffer_handle_t hCommandBuffer, ur_mem_handle_t hBuffer,
    const void *pPattern, size_t patternSize, size_t offset, size_t size,
    uint32_t numSyncPointsInWaitList,
    const ur_exp_command_buffer_sync_point_t *pSyncPointWaitList,
    ur_exp_command_buffer_sync_point_t *pSyncPoint) {

  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clCommandFillBufferKHR_fn clCommandFillBufferKHR = nullptr;
  cl_int Res = cl_ext::getExtFuncFromContext<decltype(clCommandFillBufferKHR)>(
      CLContext, cl_ext::ExtFuncPtrCache->clCommandFillBufferKHRCache,
      cl_ext::CommandFillBufferName, &clCommandFillBufferKHR);

  if (!clCommandFillBufferKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  CL_RETURN_ON_FAILURE(clCommandFillBufferKHR(
      hCommandBuffer->CLCommandBuffer, nullptr,
      cl_adapter::cast<cl_mem>(hBuffer), pPattern, patternSize, offset, size,
      numSyncPointsInWaitList, pSyncPointWaitList, pSyncPoint, nullptr));

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendUSMPrefetchExp(
    ur_exp_command_buffer_handle_t hCommandBuffer, const void *mem, size_t size,
    ur_usm_migration_flags_t flags, uint32_t numSyncPointsInWaitList,
    const ur_exp_command_buffer_sync_point_t *pSyncPointWaitList,
    ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  (void)hCommandBuffer;
  (void)mem;
  (void)size;
  (void)flags;
  (void)numSyncPointsInWaitList;
  (void)pSyncPointWaitList;
  (void)pSyncPoint;

  // Not implemented
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferAppendUSMAdviseExp(
    ur_exp_command_buffer_handle_t hCommandBuffer, const void *mem, size_t size,
    ur_usm_advice_flags_t advice, uint32_t numSyncPointsInWaitList,
    const ur_exp_command_buffer_sync_point_t *pSyncPointWaitList,
    ur_exp_command_buffer_sync_point_t *pSyncPoint) {
  (void)hCommandBuffer;
  (void)mem;
  (void)size;
  (void)advice;
  (void)numSyncPointsInWaitList;
  (void)pSyncPointWaitList;
  (void)pSyncPoint;

  // Not implemented
  return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferEnqueueExp(
    ur_exp_command_buffer_handle_t hCommandBuffer, ur_queue_handle_t hQueue,
    uint32_t numEventsInWaitList, const ur_event_handle_t *phEventWaitList,
    ur_event_handle_t *phEvent) {

  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clEnqueueCommandBufferKHR_fn clEnqueueCommandBufferKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clEnqueueCommandBufferKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clEnqueueCommandBufferKHRCache,
          cl_ext::EnqueueCommandBufferName, &clEnqueueCommandBufferKHR);

  if (!clEnqueueCommandBufferKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  const uint32_t NumberOfQueues = 1;

  CL_RETURN_ON_FAILURE(clEnqueueCommandBufferKHR(
      NumberOfQueues, cl_adapter::cast<cl_command_queue *>(&hQueue),
      hCommandBuffer->CLCommandBuffer, numEventsInWaitList,
      cl_adapter::cast<const cl_event *>(phEventWaitList),
      cl_adapter::cast<cl_event *>(phEvent)));

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferRetainCommandExp(
    ur_exp_command_buffer_command_handle_t hCommand) {
  hCommand->incrementExternalReferenceCount();
  hCommand->incrementInternalReferenceCount();
  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferReleaseCommandExp(
    ur_exp_command_buffer_command_handle_t hCommand) {
  hCommand->decrementExternalReferenceCount();
  return commandHandleReleaseInternal(hCommand);
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferUpdateKernelLaunchExp(
    ur_exp_command_buffer_command_handle_t hCommand,
    const ur_exp_command_buffer_update_kernel_launch_desc_t
        *pUpdateKernelLaunch) {

  ur_exp_command_buffer_handle_t hCommandBuffer = hCommand->hCommandBuffer;
  cl_context CLContext = cl_adapter::cast<cl_context>(hCommandBuffer->hContext);
  cl_ext::clUpdateMutableCommandsKHR_fn clUpdateMutableCommandsKHR = nullptr;
  cl_int Res =
      cl_ext::getExtFuncFromContext<decltype(clUpdateMutableCommandsKHR)>(
          CLContext, cl_ext::ExtFuncPtrCache->clUpdateMutableCommandsKHRCache,
          cl_ext::UpdateMutableCommandsName, &clUpdateMutableCommandsKHR);

  if (!clUpdateMutableCommandsKHR || Res != CL_SUCCESS)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  if (!hCommandBuffer->IsFinalized || !hCommandBuffer->IsUpdatable)
    return UR_RESULT_ERROR_INVALID_OPERATION;

  // Find the CL execution info to update
  const uint32_t NumExecInfos = pUpdateKernelLaunch->numNewExecInfos;
  const ur_exp_command_buffer_update_exec_info_desc_t *ExecInfoList =
      pUpdateKernelLaunch->pNewExecInfoList;
  std::vector<cl_mutable_dispatch_exec_info_khr> CLExecInfos;
  for (uint32_t i = 0; i < NumExecInfos; i++) {
    const ur_exp_command_buffer_update_exec_info_desc_t &URExecInfo =
        ExecInfoList[i];

    if (URExecInfo.propName == UR_KERNEL_EXEC_INFO_USM_INDIRECT_ACCESS) {
      cl_bool TrueVal = CL_TRUE;
      cl_mutable_dispatch_exec_info_khr CLExecInfo;
      CLExecInfo.param_value_size = sizeof(cl_bool);
      CLExecInfo.param_value = &TrueVal;
      CLExecInfo.param_name = CL_KERNEL_EXEC_INFO_INDIRECT_HOST_ACCESS_INTEL;
      CLExecInfos.push_back(CLExecInfo);

      CLExecInfo.param_name = CL_KERNEL_EXEC_INFO_INDIRECT_DEVICE_ACCESS_INTEL;
      CLExecInfos.push_back(CLExecInfo);

      CLExecInfo.param_name = CL_KERNEL_EXEC_INFO_INDIRECT_SHARED_ACCESS_INTEL;
      CLExecInfos.push_back(CLExecInfo);
    } else if (URExecInfo.propName == UR_KERNEL_EXEC_INFO_USM_PTRS) {
      cl_mutable_dispatch_exec_info_khr CLExecInfo{};
      CLExecInfo.param_value_size = URExecInfo.propSize;
      CLExecInfo.param_value = URExecInfo.pNewExecInfo;
      CLExecInfo.param_name = CL_KERNEL_EXEC_INFO_USM_PTRS_INTEL;
      CLExecInfos.push_back(CLExecInfo);
    } else if (URExecInfo.propName != UR_KERNEL_EXEC_INFO_CACHE_CONFIG) {
      return UR_RESULT_ERROR_INVALID_ENUMERATION;
    }
  }

  // Find the CL USM pointer arguments to the kernel.
  // WARNING - This relies on USM and SVM using the same implementation,
  // which is not guaranteed.
  // See https://github.com/KhronosGroup/OpenCL-Docs/issues/843
  const uint32_t NumPointerArgs = pUpdateKernelLaunch->numNewPointerArgs;
  const ur_exp_command_buffer_update_pointer_arg_desc_t *ArgPointerList =
      pUpdateKernelLaunch->pNewPointerArgList;
  std::vector<cl_mutable_dispatch_arg_khr> CLUSMArgs(NumPointerArgs);
  for (uint32_t i = 0; i < NumPointerArgs; i++) {
    const ur_exp_command_buffer_update_pointer_arg_desc_t &URPointerArg =
        ArgPointerList[i];
    cl_mutable_dispatch_arg_khr &USMArg = CLUSMArgs[i];
    USMArg.arg_index = URPointerArg.argIndex;
    USMArg.arg_value = *(void *const *)URPointerArg.pNewPointerArg;
  }

  // Find the memory object and scalar arguments to the kernel.
  const uint32_t NumMemobjArgs = pUpdateKernelLaunch->numNewMemObjArgs;
  const ur_exp_command_buffer_update_memobj_arg_desc_t *ArgMemobjList =
      pUpdateKernelLaunch->pNewMemObjArgList;
  const uint32_t NumValueArgs = pUpdateKernelLaunch->numNewValueArgs;
  const ur_exp_command_buffer_update_value_arg_desc_t *ArgValueList =
      pUpdateKernelLaunch->pNewValueArgList;

  std::vector<cl_mutable_dispatch_arg_khr> CLArgs;
  for (uint32_t i = 0; i < NumMemobjArgs; i++) {
    const ur_exp_command_buffer_update_memobj_arg_desc_t &URMemObjArg =
        ArgMemobjList[i];
    cl_mutable_dispatch_arg_khr CLArg{
        URMemObjArg.argIndex, // arg_index
        sizeof(cl_mem),       // arg_size
        cl_adapter::cast<const cl_mem *>(
            &URMemObjArg.hNewMemObjArg) // arg_value
    };

    CLArgs.push_back(CLArg);
  }

  for (uint32_t i = 0; i < NumValueArgs; i++) {
    const ur_exp_command_buffer_update_value_arg_desc_t &URValueArg =
        ArgValueList[i];
    cl_mutable_dispatch_arg_khr CLArg{
        URValueArg.argIndex,    // arg_index
        URValueArg.argSize,     // arg_size
        URValueArg.pNewValueArg // arg_value
    };
    CLArgs.push_back(CLArg);
  }

  const cl_uint NewWorkDim = pUpdateKernelLaunch->newWorkDim;
  cl_uint &CLWorkDim = hCommand->WorkDim;
  if (NewWorkDim != 0 && NewWorkDim != CLWorkDim) {
    // Limitation of the cl_khr_command_buffer_mutable_dispatch specification
    // that it is an error to change the ND-Range size.
    // https://github.com/KhronosGroup/OpenCL-Docs/issues/1057
    return UR_RESULT_ERROR_UNSUPPORTED_FEATURE;
  }

  // Update the ND-Range configuration of the kernel.
  const size_t CopySize = sizeof(size_t) * CLWorkDim;
  std::vector<size_t> CLGlobalWorkOffset, CLGlobalWorkSize, CLLocalWorkSize;

  if (auto GlobalWorkOffsetPtr = pUpdateKernelLaunch->pNewGlobalWorkOffset) {
    CLGlobalWorkOffset.resize(CLWorkDim);
    std::memcpy(CLGlobalWorkOffset.data(), GlobalWorkOffsetPtr, CopySize);
    if (CLWorkDim < 3) {
      const size_t ZeroSize = sizeof(size_t) * (3 - CLWorkDim);
      std::memset(CLGlobalWorkOffset.data() + CLWorkDim, 0, ZeroSize);
    }
  }

  if (auto GlobalWorkSizePtr = pUpdateKernelLaunch->pNewGlobalWorkSize) {
    CLGlobalWorkSize.resize(CLWorkDim);
    std::memcpy(CLGlobalWorkSize.data(), GlobalWorkSizePtr, CopySize);
    if (CLWorkDim < 3) {
      const size_t ZeroSize = sizeof(size_t) * (3 - CLWorkDim);
      std::memset(CLGlobalWorkSize.data() + CLWorkDim, 0, ZeroSize);
    }
  }

  if (auto LocalWorkSizePtr = pUpdateKernelLaunch->pNewLocalWorkSize) {
    CLLocalWorkSize.resize(CLWorkDim);
    std::memcpy(CLLocalWorkSize.data(), LocalWorkSizePtr, CopySize);
    if (CLWorkDim < 3) {
      const size_t ZeroSize = sizeof(size_t) * (3 - CLWorkDim);
      std::memset(CLLocalWorkSize.data() + CLWorkDim, 0, ZeroSize);
    }
  }

  cl_mutable_command_khr command =
      cl_adapter::cast<cl_mutable_command_khr>(hCommand->CLMutableCommand);
  cl_mutable_dispatch_config_khr dispatch_config = {
      CL_STRUCTURE_TYPE_MUTABLE_DISPATCH_CONFIG_KHR,
      nullptr,
      command,
      static_cast<cl_uint>(CLArgs.size()),      // num_args
      static_cast<cl_uint>(CLUSMArgs.size()),   // num_svm_args
      static_cast<cl_uint>(CLExecInfos.size()), // num_exec_infos
      CLWorkDim,                                // work_dim
      CLArgs.data(),                            // arg_list
      CLUSMArgs.data(),                         // arg_svm_list
      CLExecInfos.data(),                       // exec_info_list
      CLGlobalWorkOffset.data(),                // global_work_offset
      CLGlobalWorkSize.data(),                  // global_work_size
      CLLocalWorkSize.data(),                   // local_work_size
  };
  cl_mutable_base_config_khr config = {
      CL_STRUCTURE_TYPE_MUTABLE_BASE_CONFIG_KHR, nullptr, 1, &dispatch_config};
  CL_RETURN_ON_FAILURE(
      clUpdateMutableCommandsKHR(hCommandBuffer->CLCommandBuffer, &config));

  return UR_RESULT_SUCCESS;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferGetInfoExp(
    ur_exp_command_buffer_handle_t hCommandBuffer,
    ur_exp_command_buffer_info_t propName, size_t propSize, void *pPropValue,
    size_t *pPropSizeRet) {

  UrReturnHelper ReturnValue(propSize, pPropValue, pPropSizeRet);

  switch (propName) {
  case UR_EXP_COMMAND_BUFFER_INFO_REFERENCE_COUNT:
    return ReturnValue(hCommandBuffer->getExternalReferenceCount());
  default:
    assert(!"Command-buffer info request not implemented");
  }

  return UR_RESULT_ERROR_INVALID_ENUMERATION;
}

UR_APIEXPORT ur_result_t UR_APICALL urCommandBufferCommandGetInfoExp(
    ur_exp_command_buffer_command_handle_t hCommand,
    ur_exp_command_buffer_command_info_t propName, size_t propSize,
    void *pPropValue, size_t *pPropSizeRet) {
  UrReturnHelper ReturnValue(propSize, pPropValue, pPropSizeRet);

  switch (propName) {
  case UR_EXP_COMMAND_BUFFER_COMMAND_INFO_REFERENCE_COUNT:
    return ReturnValue(hCommand->getExternalReferenceCount());
  default:
    assert(!"Command-buffer command info request not implemented");
  }

  return UR_RESULT_ERROR_INVALID_ENUMERATION;
}

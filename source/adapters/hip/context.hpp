//===--------- context.hpp - HIP Adapter ----------------------------------===//
//
// Copyright (C) 2023 Intel Corporation
//
// Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM
// Exceptions. See LICENSE.TXT
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#pragma once

#include <set>

#include "common.hpp"
#include "device.hpp"
#include "platform.hpp"

#include <umf/memory_pool.h>

typedef void (*ur_context_extended_deleter_t)(void *UserData);

/// UR context mapping to a HIP context object.
///
/// There is no direct mapping between a HIP context and a UR context.
/// The main differences are described below:
///
/// <b> HIP context vs UR context </b>
///
/// One of the main differences between the UR API and the HIP driver API is
/// that the second modifies the state of the threads by assigning
/// `hipCtx_t` objects to threads. `hipCtx_t` objects store data associated
/// with a given device and control access to said device from the user side.
/// UR API context are objects that are passed to functions, and not bound
/// to threads.
/// The ur_context_handle_t_ object doesn't implement this behavior. It only
/// holds the HIP context data. The RAII object \ref ScopedContext implements
/// the active context behavior.
///
/// <b> Primary vs UserDefined context </b>
///
/// HIP has two different types of context, the Primary context,
/// which is usable by all threads on a given process for a given device, and
/// the aforementioned custom contexts.
/// The HIP documentation, and performance analysis, suggest using the Primary
/// context whenever possible. The Primary context is also used by the HIP
/// Runtime API. For UR applications to interop with HIP Runtime API, they have
/// to use the primary context - and make that active in the thread. The
/// `ur_context_handle_t_` object can be constructed with a `kind` parameter
/// that allows to construct a Primary or `UserDefined` context, so that
/// the UR object interface is always the same.
///
///  <b> Destructor callback </b>
///
///  Required to implement CP023, SYCL Extended Context Destruction,
///  the UR Context can store a number of callback functions that will be
///  called upon destruction of the UR Context.
///  See proposal for details.
///  https://github.com/codeplaysoftware/standards-proposals/blob/master/extended-context-destruction/index.md
///
struct ur_context_handle_t_ {

  struct deleter_data {
    ur_context_extended_deleter_t Function;
    void *UserData;

    void operator()() { Function(UserData); }
  };

  using native_type = hipCtx_t;

  ur_device_handle_t DeviceId;
  std::atomic_uint32_t RefCount;

  ur_context_handle_t_(ur_device_handle_t DevId)
      : DeviceId{DevId}, RefCount{1} {
    urDeviceRetain(DeviceId);
  };

  ~ur_context_handle_t_() { urDeviceRelease(DeviceId); }

  void invokeExtendedDeleters() {
    std::lock_guard<std::mutex> Guard(Mutex);
    for (auto &Deleter : ExtendedDeleters) {
      Deleter();
    }
  }

  void setExtendedDeleter(ur_context_extended_deleter_t Function,
                          void *UserData) {
    std::lock_guard<std::mutex> Guard(Mutex);
    ExtendedDeleters.emplace_back(deleter_data{Function, UserData});
  }

  ur_device_handle_t getDevice() const noexcept { return DeviceId; }

  uint32_t incrementReferenceCount() noexcept { return ++RefCount; }

  uint32_t decrementReferenceCount() noexcept { return --RefCount; }

  uint32_t getReferenceCount() const noexcept { return RefCount; }

  void addPool(ur_usm_pool_handle_t Pool);

  void removePool(ur_usm_pool_handle_t Pool);

  ur_usm_pool_handle_t getOwningURPool(umf_memory_pool_t *UMFPool);

private:
  std::mutex Mutex;
  std::vector<deleter_data> ExtendedDeleters;
  std::set<ur_usm_pool_handle_t> PoolHandles;
};

namespace {
/// RAII type to guarantee recovering original HIP context
/// Scoped context is used across all UR HIP plugin implementation
/// to activate the UR Context on the current thread, matching the
/// HIP driver semantics where the context used for the HIP Driver
/// API is the one active on the thread.
/// The implementation tries to avoid replacing the hipCtx_t if it cans
class ScopedContext {
  hipCtx_t Original;
  bool NeedToRecover;

public:
  ScopedContext(ur_device_handle_t hDevice) : NeedToRecover{false} {

    if (!hDevice) {
      throw UR_RESULT_ERROR_INVALID_DEVICE;
    }

    // FIXME when multi device context are supported in HIP adapter
    hipCtx_t Desired = hDevice->getNativeContext();
    UR_CHECK_ERROR(hipCtxGetCurrent(&Original));
    if (Original != Desired) {
      // Sets the desired context as the active one for the thread
      UR_CHECK_ERROR(hipCtxSetCurrent(Desired));
      if (Original == nullptr) {
        // No context is installed on the current thread
        // This is the most common case. We can activate the context in the
        // thread and leave it there until all the UR context referring to the
        // same underlying HIP context are destroyed. This emulates
        // the behaviour of the HIP runtime api, and avoids costly context
        // switches. No action is required on this side of the if.
      } else {
        NeedToRecover = true;
      }
    }
  }

  ~ScopedContext() {
    if (NeedToRecover) {
      UR_CHECK_ERROR(hipCtxSetCurrent(Original));
    }
  }
};
} // namespace
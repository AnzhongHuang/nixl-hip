/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "worker/nvshmem/nvshmem_worker.h"
#include "runtime/runtime.h"
#include "utils/utils.h"
#include <iostream>
#include <cstring>

#if HAVE_NVSHMEM && HAVE_CUDA
#define CHECK_NVSHMEM_ERROR(result, message)                                    \
    do {                                                                        \
        if (0 != result) {                                                      \
            std::cerr << "NVSHMEM: " << message << " (Error code: " << result   \
                      << ")" << std::endl;                                      \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while(0)

xferBenchNvshmemWorker::xferBenchNvshmemWorker(int *argc, char ***argv): xferBenchWorker(argc, argv) {
    // Initialize NVSHMEM
    if (XFERBENCH_RT_ETCD == xferBenchConfig::runtime_type) {
	    rank = rt->getRank();
	    size = rt->getSize();

        return;        //NVSHMEM not yet initialized
    }

    std::cout << "Runtime " << xferBenchConfig::runtime_type
		      << " not supported for NVSHMEM worker" << std::endl;
    exit(EXIT_FAILURE);
}

xferBenchNvshmemWorker::~xferBenchNvshmemWorker() {
    // Finalize NVSHMEM
    rocshmem_finalize();
}

std::optional<xferBenchIOV> xferBenchNvshmemWorker::initBasicDescNvshmem(size_t buffer_size, int mem_dev_id) {
    void *addr;

    addr = rocshmem_malloc(buffer_size);
    if (!addr) {
        std::cerr << "Failed to allocate " << buffer_size << " bytes of NVSHMEM memory" << std::endl;
        return std::nullopt;
    }

    if (isInitiator()) {
        hipMemset(addr, XFERBENCH_INITIATOR_BUFFER_ELEMENT, buffer_size);
    } else if (isTarget()) {
        hipMemset(addr, XFERBENCH_TARGET_BUFFER_ELEMENT, buffer_size);
    }

    return std::optional<xferBenchIOV>(std::in_place, (uintptr_t)addr, buffer_size, mem_dev_id);
}

void xferBenchNvshmemWorker::cleanupBasicDescNvshmem(xferBenchIOV &iov) {
    rocshmem_free((void *)iov.addr);
}

std::vector<std::vector<xferBenchIOV>> xferBenchNvshmemWorker::allocateMemory(int num_threads) {
    std::vector<std::vector<xferBenchIOV>> iov_lists;
    size_t i, buffer_size, num_devices = 0;

    if (1 != num_threads) {
        std::cerr << "NVSHMEM: Only 1 thread is supported for now" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (isInitiator()) {
        num_devices = xferBenchConfig::num_initiator_dev;
    } else if (isTarget()) {
        num_devices = xferBenchConfig::num_target_dev;
    }
    buffer_size = xferBenchConfig::total_buffer_size / (num_devices * num_threads);

    for (int list_idx = 0; list_idx < num_threads; list_idx++) {
        std::vector<xferBenchIOV> iov_list;
        for (i = 0; i < num_devices; i++) {
            std::optional<xferBenchIOV> basic_desc;
            basic_desc = initBasicDescNvshmem(buffer_size, i);
            if (basic_desc) {
                iov_list.push_back(basic_desc.value());
            }
        }
        iov_lists.push_back(iov_list);
    }
    return iov_lists;
}

void xferBenchNvshmemWorker::deallocateMemory(std::vector<std::vector<xferBenchIOV>> &iov_lists) {
    rocshmem_barrier_all();
    for (auto &iov_list: iov_lists) {
        for (auto &iov: iov_list) {
            cleanupBasicDescNvshmem(iov);
        }
    }
}

int xferBenchNvshmemWorker::exchangeMetadata() {
    // No metadata exchange needed for NVSHMEM
    return 0;
}

std::vector<std::vector<xferBenchIOV>> xferBenchNvshmemWorker::exchangeIOV(const std::vector<std::vector<xferBenchIOV>> &iov_lists) {
    // For NVSHMEM, we don't need to exchange IOV lists
    // This will just return local IOV list
    return iov_lists;
}

// No thread support for NVSHMEM yet
static int execTransfer(const std::vector<std::vector<xferBenchIOV>> &local_iovs,
                        const std::vector<std::vector<xferBenchIOV>> &remote_iovs,
                        const int num_iter, hipStream_t stream) {
    int ret = 0, tid = 0, target_rank;

    target_rank = 1;

    const auto &local_iov = local_iovs[tid];
    const auto &remote_iov = remote_iovs[tid];

    for (int i = 0; i < num_iter; i++) {
        for (size_t i = 0; i < local_iov.size(); i++) {
            auto &local = local_iov[i];
            auto &remote = remote_iov[i];
            if (XFERBENCH_OP_WRITE == xferBenchConfig::op_type) {
                rocshmem_putmem((void *)remote.addr, (void *)local.addr, local.len, target_rank);
            } else if (XFERBENCH_OP_READ == xferBenchConfig::op_type) {
                rocshmem_getmem((void *)remote.addr, (void *)local.addr, local.len, target_rank);
            }
        }
        rocshmem_quiet();
    }

    return ret;
}

std::variant<double, int> xferBenchNvshmemWorker::transfer(size_t block_size,
                                                  const std::vector<std::vector<xferBenchIOV>> &local_trans_lists,
                                                  const std::vector<std::vector<xferBenchIOV>> &remote_trans_lists) {
    hipEvent_t start_event, stop_event;
    float total_duration = 0.0;
    int num_iter = xferBenchConfig::num_iter / xferBenchConfig::num_threads;
    int skip = xferBenchConfig::warmup_iter / xferBenchConfig::num_threads;
    int ret = 0;

    // Create events to time the transfer
    CHECK_CUDA_ERROR(hipEventCreate(&start_event), "Failed to create CUDA event");
    CHECK_CUDA_ERROR(hipEventCreate(&stop_event), "Failed to create CUDA event");

    // Here the local_trans_lists is the same as remote_trans_lists
    // Reduce skip by 10x for large block sizes
    if (block_size > LARGE_BLOCK_SIZE) {
        skip /= LARGE_BLOCK_SIZE_ITER_FACTOR;
        num_iter /= LARGE_BLOCK_SIZE_ITER_FACTOR;
    }

    printf("execTransfer\n");
    ret = execTransfer(local_trans_lists, remote_trans_lists, skip, stream);
    if (ret < 0) {
        return std::variant<double, int>(ret);
    }
    rocshmem_barrier_all();
    CHECK_CUDA_ERROR(hipStreamSynchronize(stream), "Failed to synchronize CUDA stream");

    CHECK_CUDA_ERROR(hipEventRecord(start_event, stream), "Failed to record CUDA event");

    ret = execTransfer(local_trans_lists, remote_trans_lists, num_iter, stream);

    CHECK_CUDA_ERROR(hipEventRecord(stop_event, stream), "Failed to record CUDA event");

    rocshmem_barrier_all();
    CHECK_CUDA_ERROR(hipEventSynchronize(stop_event), "Failed to synchronize CUDA event");
    CHECK_CUDA_ERROR(hipStreamSynchronize(stream), "Failed to synchronize CUDA stream");

    // Time in ms
    CHECK_CUDA_ERROR(hipEventElapsedTime(&total_duration, start_event, stop_event), "Failed to get elapsed time");

    return ret < 0 ? std::variant<double, int>(ret) : std::variant<double, int>((double)total_duration * 1e+3);
}

void xferBenchNvshmemWorker::poll(size_t block_size) {
    // For NVSHMEM, we don't need to poll
    // The transfer is already complete when we reach this point
    rocshmem_barrier_all();
    CHECK_CUDA_ERROR(hipStreamSynchronize(stream), "Failed to synchronize CUDA stream");

    rocshmem_barrier_all();
    CHECK_CUDA_ERROR(hipStreamSynchronize(stream), "Failed to synchronize CUDA stream");
}

int xferBenchNvshmemWorker::synchronizeStart() {
    if (xferBenchConfig::runtime_type == XFERBENCH_RT_ETCD) {

        rocshmem_init();
        rt->broadcastInt((int *)&group_id, sizeof(rocshmem_uniqueid_t), 0);
        group_id_initialized = 1;
    }

    // Barrier to ensure all workers have initialized NVSHMEM
    rocshmem_barrier_all();

    return 0;
}

#endif

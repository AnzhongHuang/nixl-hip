# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

project('nixlbench', 'CPP', version: '0.2.0',
    default_options: ['buildtype=debug',
                'werror=true',
                'cpp_std=c++17',
                'prefix=/usr/local/nixlbench'],
    meson_version: '>= 0.64.0'
)

# set up some global vars for compiler, platform, configuration, etc.
cpp = meson.get_compiler('cpp')

# Allow overriding paths through environment variables
# CUDA
cuda_inc_path = get_option('cudapath_inc')
cuda_lib_path = get_option('cudapath_lib')
cuda_stub_path = get_option('cudapath_stub')
# ETCD
etcd_inc_path = get_option('etcd_inc_path')
etcd_lib_path = get_option('etcd_lib_path')
# NIXL
nixl_path = get_option('nixl_path')
# NVSHMEM
nvshmem_inc_path = get_option('nvshmem_inc_path')
nvshmem_lib_path = get_option('nvshmem_lib_path')
# ompi
ompi_lib_path = get_option('ompi_lib_path')
ompi_inc_path = get_option('ompi_lib_path')

# Find required libraries
# NIXL
nixl_lib_path = nixl_path + '/lib/x86_64-linux-gnu'
nixl_lib = cpp.find_library('nixl', dirs: [nixl_lib_path])
nixl_build = cpp.find_library('nixl_build', dirs: [nixl_lib_path])
nixl_serdes = cpp.find_library('serdes', dirs: [nixl_lib_path])


if not nixl_lib.found() or not nixl_build.found() or not nixl_serdes.found()
    error('NIXL Libraries not found. Exiting.')
endif

# CUDA
cuda_available = false
if cuda_lib_path == ''
    cuda_dep = dependency('cuda', required : false, modules : [ 'hiprtc', 'amdhip64' ])
    if cuda_dep.found()
        cuda_available = true
    endif
else
    message('cuda lib path ', cuda_lib_path)
    if cuda_stub_path == ''
        cuda_stub_path = cuda_lib_path + '/stubs'
    endif
    cuda_dep = declare_dependency(
        link_args : ['-L' + cuda_lib_path, '-L' + cuda_stub_path, '-lamdhip64', '-lhiprtc'],
        include_directories : include_directories(cuda_inc_path))
    cuda_available = true
endif

# UCX
ucx_dep = dependency('ucx')

# GFlags
gflags_dep = dependency('gflags', required: true)

# OpenMP
openmp_dep = dependency('openmp', required: true)

# Check for etcd-cpp-api - use multiple methods for discovery
etcd_dep = dependency('etcd-cpp-api', required : false)

# Ensure etcd is available
etcd_available = etcd_dep.found()
if etcd_available
    add_project_arguments('-DHAVE_ETCD', language: 'cpp')
else
    message('ETCD C++ client library not found. Disabling ETCD runtime.')
endif

etcd_inc = etcd_inc_path
nixl_inc = nixl_path + '/include'
nvshmem_inc = nvshmem_inc_path
ompi_inc = '/usr/local/ompi/include'
inc_dir = include_directories('.', './src/', nixl_inc, ompi_inc)

nvshmem_available = false
if nvshmem_lib_path != ''
    if cpp.has_header('rocshmem/rocshmem.hpp', include_directories: include_directories(nvshmem_inc))
        nvshmem_lib = cpp.find_library('rocshmem', dirs: [nvshmem_lib_path])
        #nvshmem_host_lib = cpp.find_library('nvshmem_host', dirs: [nvshmem_lib_path])
        if nvshmem_lib.found()
            nvshmem_available = true
            inc_dir = include_directories('.', './src/', nixl_inc, nvshmem_inc, ompi_inc)
            add_project_arguments('-DHAVE_NVSHMEM', language: 'cpp')
            add_project_arguments('-Wno-unused-variable', language: 'cpp')
        else
            message('cannot find nvshmem', nvshmem_lib_path)
        endif
    endif
endif

if cuda_available
    add_project_arguments('-DHAVE_CUDA', language: 'cpp')
    add_project_arguments('-D__HIP_PLATFORM_AMD__', '-Wno-deprecated-declarations', '-Wno-unused-result', language: 'cpp')
endif

# Subprojects
subdir('src/utils')
subdir('src/runtime')
subdir('src/worker')

# Configure header file
configure_file(
    output: 'config.h',
    configuration: {
        'HAVE_ETCD': etcd_available ? '1' : '0',
        'HAVE_NVSHMEM': nvshmem_available ? '1' : '0',
        'HAVE_CUDA': cuda_available ? '1' : '0',
    },
    install: true,
    install_dir: get_option('includedir') / 'nixlbench'
)

deps = [gflags_dep, nixl_lib, nixl_build, nixl_serdes, openmp_dep]
args = []
if etcd_available
    deps += [etcd_dep]
endif
if cuda_available
    deps += [cuda_dep]
endif
if nvshmem_available
    deps += [nvshmem_lib]
    args += [
        '-Xcompiler', '-fopenmp',
        '-Xlinker', '-rpath=/opt/rocm/lib',
        '-Xlinker', '--allow-shlib-undefined',
        '-lamdhip64',
        '-lhiprtc',
        #'-g', '-O0',
    ]
endif

if not etcd_available
    error('No runtime available or not found')
endif

if nvshmem_available
    # Use nvcc directly for compilation and linking
    nvcc = find_program('hipcc')
    nvcc_args = []
    if etcd_available
        if etcd_inc != ''
            nvcc_args += ['-I' + etcd_inc]
        endif
    endif
    nvcc_args += ['-I' + nixl_inc]
    nvcc_args += ['-I' + ompi_inc]
    nvcc_args += ['-I' + nvshmem_inc]
    nvcc_args += ['-I.', '-I./src/', '-I../src/']
    nvcc_args += ['-I' + meson.current_build_dir()]
    if etcd_available
        if etcd_lib_path != ''
            nvcc_args += ['-L' + etcd_lib_path]
        endif
    endif
    nvcc_args += ['-L' + nixl_lib_path]
    nvcc_args += ['-L' + nvshmem_lib_path]
    nvcc_args += ['-L' + ompi_lib_path]
    nvcc_args += ['-L/usr/local/lib']
    nvcc_args += ['-L' + meson.current_build_dir() + '/src/utils']
    nvcc_args += ['-L' + meson.current_build_dir() + '/src/runtime']
    nvcc_args += ['-L' + meson.current_build_dir() + '/src/worker']
    nvcc_args += ['-lnixl', '-lnixl_build', '-lserdes', '-lgflags',  '-lamdhip64', '-lhiprtc', '-lhsa-runtime64', '-lrocshmem', '-lmpi']
    nvcc_args += ['-Xlinker', '-rpath=/opt/rocm/lib']
    nvcc_args += ['--hip-link', '-fgpu-rdc', '--offload-arch=gfx942:xnack-']
    nvcc_args += args
    nvcc_cmd_files = [
                 meson.current_build_dir() + '/../src/utils/utils.cpp',
                 meson.current_build_dir() + '/../src/runtime/runtime.cpp',
                 meson.current_build_dir() + '/../src/worker/worker.cpp',
                 meson.current_build_dir() + '/../src/worker/nixl/nixl_worker.cpp',
                 meson.current_build_dir() + '/../src/worker/nvshmem/nvshmem_worker.cpp'
                 ]

    if etcd_available
        nvcc_args += ['-letcd-cpp-api', '-lcpprest']
        etcd_rt = meson.current_build_dir() + '/../src/runtime/etcd/etcd_rt.cpp'
        nvcc_cmd_files += etcd_rt
    endif

    nvcc_command = [nvcc, nvcc_args, '-o', '@OUTPUT@', '@INPUT@']
    nvcc_command += nvcc_cmd_files

    custom_target('nixlbench',
        input: 'src/main.cpp',
        output: 'nixlbench',
        command: nvcc_command,
        build_by_default: true,
        install: true,
        install_dir: get_option('bindir'),
        depends: [nixlbench_runtimes, utils_lib, worker_libs])
else
    executable('nixlbench', 'src/main.cpp',
                include_directories: inc_dir,
                link_with: [nixlbench_runtimes, utils_lib, worker_libs],
                dependencies: deps,
                link_args: args,
                install: true,
                install_dir: get_option('bindir'))
endif

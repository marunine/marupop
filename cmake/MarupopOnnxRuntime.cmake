# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# marupop::onnxruntime for the Windows build, from the upstream ONNX Runtime release archive.
#
# The upstream archive replaces the vcpkg onnxruntime port at the vcpkg.json baseline:
# - The port provides ONNX Runtime 1.23.2. The Arch Linux package provides ONNX Runtime 1.29.0.
# - The port lists 22 dependencies, which vcpkg builds from source.
# - vcpkg's scripts/ci.baseline.txt lists the port as failing on x64-windows.
#
# The archive holds no CMake package configuration file.
set(MARUPOP_ONNXRUNTIME_VERSION 1.29.1)
set(MARUPOP_ONNXRUNTIME_SHA256 999a6ae6f70cd773b62a407d9a16f41bc2090bfda84f5fcee3a9268938b52a7a)
set(onnxruntime_root_doc "Unpacked onnxruntime-win-x64 release folder; empty selects the pinned download")
set(ONNXRUNTIME_ROOT "" CACHE PATH "${onnxruntime_root_doc}")

if(NOT ONNXRUNTIME_ROOT)
    set(archive_name onnxruntime-win-x64-${MARUPOP_ONNXRUNTIME_VERSION})
    set(unpacked ${CMAKE_BINARY_DIR}/_deps/${archive_name})
    if(NOT EXISTS ${unpacked}/include/onnxruntime_cxx_api.h)
        set(archive ${CMAKE_BINARY_DIR}/_deps/${archive_name}.zip)
        message(STATUS "Downloading ONNX Runtime ${MARUPOP_ONNXRUNTIME_VERSION}")
        file(DOWNLOAD
             https://github.com/microsoft/onnxruntime/releases/download/v${MARUPOP_ONNXRUNTIME_VERSION}/${archive_name}.zip
             ${archive}
             EXPECTED_HASH SHA256=${MARUPOP_ONNXRUNTIME_SHA256}
             STATUS download_status
             TLS_VERIFY ON)
        list(GET download_status 0 download_code)
        if(NOT download_code EQUAL 0)
            list(GET download_status 1 download_message)
            message(FATAL_ERROR "Could not download ONNX Runtime (${download_message}). "
                                "Set ONNXRUNTIME_ROOT to an unpacked ${archive_name}.zip.")
        endif()
        file(ARCHIVE_EXTRACT INPUT ${archive} DESTINATION ${CMAKE_BINARY_DIR}/_deps)
        file(REMOVE ${archive})
    endif()
    set(ONNXRUNTIME_ROOT ${unpacked} CACHE PATH "${onnxruntime_root_doc}" FORCE)
endif()

if(NOT EXISTS ${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h OR NOT EXISTS ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll)
    message(FATAL_ERROR "ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT} lacks include/onnxruntime_cxx_api.h or "
                        "lib/onnxruntime.dll")
endif()

add_library(marupop_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(marupop_onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll
    IMPORTED_IMPLIB ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib
    INTERFACE_INCLUDE_DIRECTORIES ${ONNXRUNTIME_ROOT}/include)
add_library(marupop::onnxruntime ALIAS marupop_onnxruntime)
set(onnxruntime_dlls ${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll ${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll)

# vcpkg's applocal step copies the DLLs of vcpkg packages alone. The loader searches the
# executable folder before System32, where Windows installs its own onnxruntime.dll.
add_custom_target(marupop_onnxruntime_dll ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${onnxruntime_dlls} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Copying the ONNX Runtime DLLs into ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    VERBATIM)
install(FILES ${onnxruntime_dlls} DESTINATION ${KDE_INSTALL_BINDIR})

set(MARUPOP_ONNXRUNTIME_NOTICES ${ONNXRUNTIME_ROOT}/LICENSE ${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt)

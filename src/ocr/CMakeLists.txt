# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
target_sources(marupop_lib PRIVATE
    backend.h
    grouping.cpp
    grouping.h
    hittest.cpp
    hittest.h
    meikiocrbackend.cpp
    meikicorrections.cpp
    meikicorrections.h
    meikicorrectiontable.h
    meikiocrbackend.h
    meikipostprocess.cpp
    meikipostprocess.h
    meikipreprocess.cpp
    meikipreprocess.h
    modelstore.cpp
    modelstore.h
    ocrservice.cpp
    ocrservice.h
    ocrtypes.h
    ortenv.cpp
    ortenv.h
    ortenv_p.h
    resolution.cpp
    resolution.h
    screenaibackend.cpp
    screenaibackend.h
    screenaiproto.cpp
    screenaiproto.h)

# The project builds with -fno-exceptions (KDECompilerSettings). These three translation units
# are the ones that call a throwing API: the ONNX Runtime C++ wrapper reports every error by
# throwing Ort::Exception, and cv::resize throws cv::Exception. Each catches std::exception at
# its own entry points, so no exception crosses the module boundary. The flag is appended after
# the KDE settings, and the last -f(no-)exceptions on a compile line wins.
#
# TARGET_DIRECTORY names the scope the property has to land in: source-file properties are
# scoped to the directory that sets them (CMP0118), and marupop_lib is defined one directory up
# in src/CMakeLists.txt, so a plain set_source_files_properties() here would be ignored.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/meikiocrbackend.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/meikipreprocess.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/ortenv.cpp
                            TARGET_DIRECTORY marupop_lib
                            PROPERTIES COMPILE_OPTIONS "-fexceptions")

# ONNX Runtime and OpenCV stay private: no header of this module names a type from either, so
# neither reaches another module's include path. ${CMAKE_DL_LIBS} is for the dlopen() call that
# loads the Screen AI component.
target_link_libraries(marupop_lib PRIVATE
    PkgConfig::ONNXRUNTIME
    opencv_core
    opencv_imgproc
    ${CMAKE_DL_LIBS}
    Qt6::Network
    KF6::CoreAddons)

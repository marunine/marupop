// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The ONNX Runtime facts the rest of the application reads, behind a header that pulls in no
// ONNX Runtime declaration: onnxruntime_cxx_api.h throws, and every translation unit outside
// ocr/ compiles with -fno-exceptions. ocr/ortenv_p.h is the header that exposes Ort::Env, and
// only the -fexceptions translation units listed in ocr/CMakeLists.txt include it.
#pragma once

#include <QString>
#include <QStringList>

namespace maru::ocr
{

// Ort::GetAvailableProviders(), which lists the execution providers compiled into the
// installed libonnxruntime.so. An entry appearing here is not proof that the provider loads:
// onnxruntime-rocm 1.29.0 lists DnnlExecutionProvider and fails to load
// libonnxruntime_providers_dnnl.so at session construction.
[[nodiscard]] QStringList availableProviders();

// OrtGetApiBase()->GetVersionString(), for example "1.29.0".
[[nodiscard]] QString ortVersion();

} // namespace maru::ocr

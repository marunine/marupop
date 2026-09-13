// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// ONNX Runtime C++ wrapper access, for the -fexceptions translation units alone. Including
// this header from a -fno-exceptions translation unit fails to compile, which is the intended
// guard: onnxruntime_cxx_api.h reports errors by throwing Ort::Exception.
#pragma once

#include <QString>

#include <onnxruntime_cxx_api.h>

namespace maru::ocr
{

// The one Ort::Env of the process. It owns the global thread pool and the logger; a second one
// is legal and allocates a second pool. Constructed on first call and never destroyed, because
// ONNX Runtime requires every Ort::Session to be destroyed before its Ort::Env and a static
// destruction order across translation units cannot guarantee that.
Ort::Env &ortEnv();

// Execution providers in preference order.
// CPUExecutionProvider is always last and is the only entry that needs no append call.
inline constexpr const char *kProviderPreference[] = {
    "CUDAExecutionProvider",
    "ROCMExecutionProvider",
    "MIGraphXExecutionProvider",
    "DnnlExecutionProvider",
};

// Appends one provider to sessionOptions through the OrtApi struct members, which are present
// in every build because the struct layout is version-stable. A provider missing from the
// build reports a non-null OrtStatus that the C++ wrapper turns into an Ort::Exception, which
// this function catches: the return value is false and the session then builds on CPU.
[[nodiscard]] bool appendProvider(Ort::SessionOptions &sessionOptions, const QString &provider);

} // namespace maru::ocr

// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/ortenv.h"

#include "core/logging.h"
#include "ocr/ortenv_p.h"

#include <QLoggingCategory>

#include <onnxruntime_session_options_config_keys.h>

namespace maru::ocr
{

Ort::Env &ortEnv()
{
    // ORT_LOGGING_LEVEL_WARNING rather than the default: the D-FINE graphs produce a page of
    // info-level notes about constant folding at every session construction.
    static auto *env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "marupop");
    return *env;
}

QStringList availableProviders()
{
    QStringList providers;
    try {
        for (const std::string &provider : Ort::GetAvailableProviders()) {
            providers.append(QString::fromStdString(provider));
        }
    } catch (const std::exception &error) {
        qCWarning(logMeikiOcr) << "cannot list the execution providers:" << error.what();
    }
    return providers;
}

QString ortVersion()
{
    const char *version = OrtGetApiBase()->GetVersionString();
    return version != nullptr ? QString::fromUtf8(version) : QString{};
}

bool appendProvider(Ort::SessionOptions &sessionOptions, const QString &provider)
{
    // The MIGraphX provider factory logs through LoggingManager::DefaultLogger(), which the
    // first Ort::Env registers. Appending before any Ort::Env exists throws "Attempt to use
    // DefaultLogger but none has been registered" and hides the real availability answer.
    ortEnv();
    try {
        if (provider == QLatin1String("CUDAExecutionProvider")) {
            OrtCUDAProviderOptions options{};
            options.device_id = 0;
            sessionOptions.AppendExecutionProvider_CUDA(options);
            return true;
        }
        if (provider == QLatin1String("ROCMExecutionProvider")) {
            OrtROCMProviderOptions options{};
            options.device_id = 0;
            sessionOptions.AppendExecutionProvider_ROCM(options);
            return true;
        }
        if (provider == QLatin1String("MIGraphXExecutionProvider")) {
            // Every field is set: OrtMIGraphXProviderOptions holds const char * members that
            // the provider dereferences, and a zero-initialized struct leaves them null.
            OrtMIGraphXProviderOptions options{};
            options.device_id = 0;
            options.migraphx_fp16_enable = 0;
            options.migraphx_fp8_enable = 0;
            options.migraphx_int8_enable = 0;
            options.migraphx_use_native_calibration_table = 0;
            options.migraphx_int8_calibration_table_name = "";
            options.migraphx_save_compiled_model = 0;
            options.migraphx_save_model_path = "";
            options.migraphx_load_compiled_model = 0;
            options.migraphx_load_model_path = "";
            options.migraphx_exhaustive_tune = false;
            options.migraphx_mem_limit = 0;
            sessionOptions.AppendExecutionProvider_MIGraphX(options);
            // Reject a GPU provider that cannot cover the graph instead of accepting fragmented GPU
            // execution with intervening CPU operations. Unsupported partitions can incur expensive
            // deferred compilation or fail only on the first inference. Session construction should
            // fail early so provider selection can continue to its CPU fallback.
            sessionOptions.AddConfigEntry(kOrtSessionOptionsDisableCPUEPFallback, "1");
            return true;
        }
        if (provider == QLatin1String("DnnlExecutionProvider")) {
            // OrtDnnlProviderOptions is opaque in the Arch builds, so the legacy C entry point
            // is the only way to reach the provider. It is an exported dynamic symbol, present
            // because onnxruntime_c_api.h declares it and every Arch variant exports it.
            OrtStatus *status =
                OrtSessionOptionsAppendExecutionProvider_Dnnl(static_cast<OrtSessionOptions *>(sessionOptions), 1);
            if (status != nullptr) {
                qCDebug(logMeikiOcr) << "dnnl unavailable:" << Ort::GetApi().GetErrorMessage(status);
                Ort::GetApi().ReleaseStatus(status);
                return false;
            }
            return true;
        }
    } catch (const std::exception &error) {
        qCDebug(logMeikiOcr) << provider << "unavailable:" << error.what();
        return false;
    }
    return false;
}

} // namespace maru::ocr

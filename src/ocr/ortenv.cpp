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
            // Through the OrtApi table rather than the legacy OrtSessionOptionsAppendExecutionProvider_Dnnl
            // entry point: onnxruntime_c_api.h declares that symbol for every build, but only a
            // build with DNNL compiled in exports it, so onnxruntime-cpu fails to link against it.
            // The table entries exist in every build and return a status when DNNL is absent.
            // onnxruntime_c_api.h only declares OrtDnnlProviderOptions. Its definition is in
            // dnnl_provider_options.h, which no ORT header includes and onnxruntime-cpu does not
            // ship, so CreateDnnlProviderOptions allocates it.
            const OrtApi &api = Ort::GetApi();
            OrtDnnlProviderOptions *options = nullptr;
            OrtStatus *status = api.CreateDnnlProviderOptions(&options);
            if (status == nullptr) {
                const char *const keys[] = {"use_arena"};
                const char *const values[] = {"1"};
                status = api.UpdateDnnlProviderOptions(options, keys, values, 1);
            }
            if (status == nullptr) {
                status = api.SessionOptionsAppendExecutionProvider_Dnnl(sessionOptions, options);
            }
            api.ReleaseDnnlProviderOptions(options);
            if (status != nullptr) {
                qCDebug(logMeikiOcr) << "dnnl unavailable:" << api.GetErrorMessage(status);
                api.ReleaseStatus(status);
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

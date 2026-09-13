// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/meikiocrbackend.h"

#include "core/logging.h"
#include "core/settings.h"
#include "ocr/meikicorrections.h"
#include "ocr/meikipostprocess.h"
#include "ocr/meikipreprocess.h"
#include "ocr/modelstore.h"
#include "ocr/ortenv.h"
#include "ocr/ortenv_p.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QLoggingCategory>
#include <QSet>

#include <KLocalizedString>

#include <algorithm>

namespace maru::ocr
{

namespace
{

constexpr QLatin1StringView kCpuProvider{"CPUExecutionProvider"};

// Providers whose session construction or validation inference failed in this process, so each
// failure is paid once per run. A cache across runs would have to key on the ONNX Runtime
// version and every model digest, which is deferred.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming)
QSet<QString> g_failedProviders;

// Input and output names of one session, and the const char * arrays Run() takes. The pointers
// index the strings, so neither vector is modified after fill().
struct ModelIo
{
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<const char *> inputs;
    std::vector<const char *> outputs;

    void fill(Ort::Session &session)
    {
        Ort::AllocatorWithDefaultOptions allocator;
        for (size_t index = 0; index < session.GetInputCount(); ++index) {
            inputNames.emplace_back(session.GetInputNameAllocated(index, allocator).get());
        }
        for (size_t index = 0; index < session.GetOutputCount(); ++index) {
            outputNames.emplace_back(session.GetOutputNameAllocated(index, allocator).get());
        }
        for (const std::string &name : inputNames) {
            inputs.push_back(name.c_str());
        }
        for (const std::string &name : outputNames) {
            outputs.push_back(name.c_str());
        }
    }
};

Ort::SessionOptions makeSessionOptions(int intraOpThreads)
{
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(std::clamp(intraOpThreads, 1, 32));
    // The three sessions run one after another, so an inter-op pool would only add threads
    // that never have two branches to run in parallel.
    options.SetInterOpNumThreads(1);
    // Without these two entries the thread pool busy-spins between inferences, which costs one
    // core for the whole life of a tray-resident process.
    options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    options.AddConfigEntry("session.inter_op.allow_spinning", "0");
    // Keep the CPU arena enabled so inference can reuse intermediate tensor allocations.
    // This trades retained memory for lower allocation overhead during repeated scans.
    return options;
}

// One detection-shaped inference over a zeroed tensor. A provider whose graph has a dynamic
// input dimension compiles at the first Run() rather than at session construction, which is
// why the session is exercised before it is trusted.
bool validate(Ort::Session &session, const ModelIo &io, QSize target)
{
    const std::vector<int64_t> imageShape = {1, 3, target.height(), target.width()};
    std::vector<float> image(
        static_cast<size_t>(3) * static_cast<size_t>(target.width()) * static_cast<size_t>(target.height()), 0.0F);
    const std::vector<int64_t> sizeShape = {1, 2};
    std::vector<int64_t> sizes = {target.width(), target.height()};
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> inputs;
    inputs.push_back(
        Ort::Value::CreateTensor<float>(memory, image.data(), image.size(), imageShape.data(), imageShape.size()));
    inputs.push_back(
        Ort::Value::CreateTensor<int64_t>(memory, sizes.data(), sizes.size(), sizeShape.data(), sizeShape.size()));
    const std::vector<Ort::Value> outputs = session.Run(
        Ort::RunOptions{nullptr}, io.inputs.data(), inputs.data(), inputs.size(), io.outputs.data(), io.outputs.size());
    return outputs.size() == 3;
}

} // namespace

// The three sessions, their input and output names, and the tensors reused across calls. A
// detection tensor is 960 * 544 * 3 floats, 6.3 MB, and a batch-8 horizontal recognition tensor
// is 2.95 MB: both are allocated once rather than at every frame.
struct MeikiOcrBackend::Sessions
{
    std::unique_ptr<Ort::Session> detection;
    std::unique_ptr<Ort::Session> horizontal;
    std::unique_ptr<Ort::Session> vertical;
    ModelIo detectionIo;
    ModelIo horizontalIo;
    ModelIo verticalIo;
    QString provider;
    std::vector<float> detectionTensor;
    std::vector<float> recognitionTensor;
    std::vector<int64_t> targetSizes;
};

MeikiOcrBackend::Options MeikiOcrBackend::optionsFromSettings()
{
    return Options{.detectionThreshold = static_cast<float>(PopSettings::meikiDetectionThreshold()),
                   .recognitionThreshold = static_cast<float>(PopSettings::meikiRecognitionThreshold()),
                   .punctuationFactor = static_cast<float>(PopSettings::meikiPunctuationConfidenceFactor()),
                   .intraOpThreads = PopSettings::meikiIntraOpThreads(),
                   .allowGpu = PopSettings::meikiAllowGpu(),
                   .useSmallDetector = PopSettings::meikiUseSmallDetector(),
                   .applyCorrections = PopSettings::meikiApplyCorrections()};
}

MeikiOcrBackend::MeikiOcrBackend(QString modelDirectory, Options options)
    : m_modelDirectory(std::move(modelDirectory))
    , m_options(options)
{}

MeikiOcrBackend::~MeikiOcrBackend() = default;

QString MeikiOcrBackend::displayName()
{
    return QStringLiteral("meikiocr");
}

QString MeikiOcrBackend::name() const
{
    return displayName();
}

bool MeikiOcrBackend::isReady() const
{
    return m_sessions != nullptr;
}

QString MeikiOcrBackend::activeProvider() const
{
    return m_sessions != nullptr ? m_sessions->provider : QString{};
}

void MeikiOcrBackend::setOptions(const Options &options)
{
    m_options = options;
}

bool MeikiOcrBackend::modelsPresent(const QString &directory)
{
    return ModelStore::requiredModelsPresentIn(directory);
}

bool MeikiOcrBackend::initialize()
{
    if (m_sessions != nullptr) {
        return true;
    }
    const ModelRole detectionRole = m_options.useSmallDetector ? ModelRole::SmallDetection : ModelRole::Detection;
    const QString detectionPath = m_modelDirectory + QLatin1Char('/') + ModelStore::model(detectionRole).fileName;
    const QString horizontalPath =
        m_modelDirectory + QLatin1Char('/') + ModelStore::model(ModelRole::HorizontalRecognition).fileName;
    const QString verticalPath =
        m_modelDirectory + QLatin1Char('/') + ModelStore::model(ModelRole::VerticalRecognition).fileName;
    for (const QString &path : {detectionPath, horizontalPath, verticalPath}) {
        if (!QFileInfo::exists(path)) {
            qCWarning(logMeikiOcr) << "the model" << path << "is missing";
            return false;
        }
    }

    const QSize detectionTarget = m_options.useSmallDetector ? QSize{kSmallDetectionWidth, kSmallDetectionHeight}
                                                             : QSize{kDetectionWidth, kDetectionHeight};

    // GPU providers are opt-in and attempted in the preference order defined by ortenv.
    // Provider initialization can fail for unsupported graphs; the CPU provider remains
    // available as the fallback.
    QStringList attempts;
    if (m_options.allowGpu) {
        const QStringList available = availableProviders();
        for (const char *candidate : kProviderPreference) {
            const QString provider = QString::fromLatin1(candidate);
            if (available.contains(provider) && !g_failedProviders.contains(provider)) {
                attempts.append(provider);
            }
        }
    }
    attempts.append(kCpuProvider);

    for (const QString &provider : std::as_const(attempts)) {
        auto sessions = std::make_unique<Sessions>();
        sessions->provider = provider;
        try {
            Ort::SessionOptions options = makeSessionOptions(m_options.intraOpThreads);
            if (provider != kCpuProvider && !appendProvider(options, provider)) {
                continue;
            }
            sessions->detection = std::make_unique<Ort::Session>(ortEnv(), qPrintable(detectionPath), options);
            sessions->horizontal = std::make_unique<Ort::Session>(ortEnv(), qPrintable(horizontalPath), options);
            sessions->vertical = std::make_unique<Ort::Session>(ortEnv(), qPrintable(verticalPath), options);
            sessions->detectionIo.fill(*sessions->detection);
            sessions->horizontalIo.fill(*sessions->horizontal);
            sessions->verticalIo.fill(*sessions->vertical);
            if (!validate(*sessions->detection, sessions->detectionIo, detectionTarget)) {
                throw std::runtime_error("the validation inference returned an unexpected output count");
            }
        } catch (const std::exception &error) {
            qCWarning(logMeikiOcr) << provider << "is unusable:" << error.what();
            if (provider != kCpuProvider) {
                g_failedProviders.insert(provider);
            }
            continue;
        }
        m_sessions = std::move(sessions);
        qCDebug(logMeikiOcr) << "meikiocr ready on" << provider << "with" << m_options.intraOpThreads
                             << "intra-op threads";
        return true;
    }
    qCWarning(logMeikiOcr) << "no execution provider could run the detection model";
    return false;
}

Result MeikiOcrBackend::recognize(const QImage &image)
{
    Result result;
    result.backendName = name();
    result.sourceSize = image.size();
    if (m_sessions == nullptr && !initialize()) {
        result.errorMessage = i18nc("@info", "The meikiocr models are not loaded.");
        return result;
    }
    if (image.isNull() || image.width() < 1 || image.height() < 1) {
        result.errorMessage = i18nc("@info", "The captured image is empty.");
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    const QImage rgb = toRgb888(image);
    const QSize target = m_options.useSmallDetector ? QSize{kSmallDetectionWidth, kSmallDetectionHeight}
                                                    : QSize{kDetectionWidth, kDetectionHeight};
    // The whole image is letterboxed into target, so target plus Result::sourceSize is what
    // ocr::charExtentAtModelInput() reconstructs the detection scale from.
    result.modelInputSize = target;
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Sessions &sessions = *m_sessions;

    try {
        // Detection over the whole image, letterboxed into the model input.
        const Letterbox letterbox = detectionLetterbox(rgb.size(), target);
        sessions.detectionTensor.resize(static_cast<size_t>(3) * static_cast<size_t>(target.width()) *
                                        static_cast<size_t>(target.height()));
        if (!packDetectionTensor(rgb, target, letterbox, sessions.detectionTensor.data())) {
            result.errorMessage = i18nc("@info", "Could not resize the captured image for text recognition.");
            return result;
        }
        const std::vector<int64_t> detectionShape = {1, 3, target.height(), target.width()};
        const std::vector<int64_t> sizeShape = {1, 2};
        // orig_target_sizes is [width, height] of the original image implied by the letterbox
        // scale, which is what makes the graph return source-image coordinates.
        sessions.targetSizes = {static_cast<int64_t>(target.width() / letterbox.scale),
                                static_cast<int64_t>(target.height() / letterbox.scale)};
        std::vector<Ort::Value> detectionInputs;
        detectionInputs.push_back(Ort::Value::CreateTensor<float>(memory,
                                                                  sessions.detectionTensor.data(),
                                                                  sessions.detectionTensor.size(),
                                                                  detectionShape.data(),
                                                                  detectionShape.size()));
        detectionInputs.push_back(Ort::Value::CreateTensor<int64_t>(
            memory, sessions.targetSizes.data(), sessions.targetSizes.size(), sizeShape.data(), sizeShape.size()));
        const std::vector<Ort::Value> detectionOutputs = sessions.detection->Run(Ort::RunOptions{nullptr},
                                                                                 sessions.detectionIo.inputs.data(),
                                                                                 detectionInputs.data(),
                                                                                 detectionInputs.size(),
                                                                                 sessions.detectionIo.outputs.data(),
                                                                                 sessions.detectionIo.outputs.size());
        // Output 0 is labels, which meikiocr does not read.
        const auto boxShape = detectionOutputs.at(1).GetTensorTypeAndShapeInfo().GetShape();
        const int slots = boxShape.size() >= 2 ? static_cast<int>(boxShape.at(1)) : 0;
        const QList<QRect> boxes = detectionBoxes(detectionOutputs.at(1).GetTensorData<float>(),
                                                  detectionOutputs.at(2).GetTensorData<float>(),
                                                  slots,
                                                  rgb.size(),
                                                  m_options.detectionThreshold);

        // meikiocr's orientation rule for the recognition pass. ocr::groupLines() applies a
        // stricter rule later; both exist and neither replaces the other.
        QList<qsizetype> horizontalBoxes;
        QList<qsizetype> verticalBoxes;
        for (qsizetype index = 0; index < boxes.size(); ++index) {
            const QRect &box = boxes.at(index);
            if (box.width() <= 0 || box.height() <= 0) {
                continue;
            }
            if (box.height() > box.width()) {
                verticalBoxes.append(index);
            } else {
                horizontalBoxes.append(index);
            }
        }

        QHash<qsizetype, QList<Candidate>> candidates;

        // Horizontal recognition, in chunks of kMaxBatchSize.
        struct HorizontalItem
        {
            qsizetype boxIndex;
            HorizontalCrop crop;
        };

        QList<HorizontalItem> horizontalItems;
        for (const qsizetype index : std::as_const(horizontalBoxes)) {
            const HorizontalCrop crop = horizontalCrop(boxes.at(index));
            if (crop.effectiveWidth >= 1 && crop.effectiveHeight >= 1) {
                horizontalItems.append(HorizontalItem{.boxIndex = index, .crop = crop});
            }
        }
        const size_t horizontalStride = static_cast<size_t>(3) * kRecognitionHeight * kRecognitionWidth;
        for (qsizetype start = 0; start < horizontalItems.size(); start += kMaxBatchSize) {
            const qsizetype count = std::min<qsizetype>(kMaxBatchSize, horizontalItems.size() - start);
            sessions.recognitionTensor.resize(static_cast<size_t>(count) * horizontalStride);
            qsizetype packed = 0;
            QList<HorizontalItem> rows;
            for (qsizetype offset = 0; offset < count; ++offset) {
                const HorizontalItem &item = horizontalItems.at(start + offset);
                float *destination =
                    sessions.recognitionTensor.data() + (static_cast<size_t>(packed) * horizontalStride);
                if (!packHorizontalTensor(rgb, item.crop, destination)) {
                    continue;
                }
                rows.append(item);
                ++packed;
            }
            if (packed == 0) {
                continue;
            }
            const std::vector<int64_t> imageShape = {packed, 3, kRecognitionHeight, kRecognitionWidth};
            // The second input is a constant: the padded model input extent, [960, 32]. A
            // matching [N,2] runs 19 % faster than the [1,2] the Python code passes.
            sessions.targetSizes.clear();
            for (qsizetype row = 0; row < packed; ++row) {
                sessions.targetSizes.push_back(kRecognitionWidth);
                sessions.targetSizes.push_back(kRecognitionHeight);
            }
            const std::vector<int64_t> rowShape = {packed, 2};
            std::vector<Ort::Value> inputs;
            inputs.push_back(Ort::Value::CreateTensor<float>(memory,
                                                             sessions.recognitionTensor.data(),
                                                             static_cast<size_t>(packed) * horizontalStride,
                                                             imageShape.data(),
                                                             imageShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                memory, sessions.targetSizes.data(), sessions.targetSizes.size(), rowShape.data(), rowShape.size()));
            const std::vector<Ort::Value> outputs = sessions.horizontal->Run(Ort::RunOptions{nullptr},
                                                                             sessions.horizontalIo.inputs.data(),
                                                                             inputs.data(),
                                                                             inputs.size(),
                                                                             sessions.horizontalIo.outputs.data(),
                                                                             sessions.horizontalIo.outputs.size());
            // char_codes is int32 while the detection labels are int64; the dtypes differ
            // between the two graphs.
            const auto *codes = outputs.at(0).GetTensorData<int32_t>();
            const auto *charBoxes = outputs.at(1).GetTensorData<float>();
            const auto *scores = outputs.at(2).GetTensorData<float>();
            const auto shape = outputs.at(0).GetTensorTypeAndShapeInfo().GetShape();
            const int slotsPerRow = shape.size() >= 2 ? static_cast<int>(shape.at(1)) : 0;
            for (qsizetype row = 0; row < rows.size(); ++row) {
                const HorizontalItem &item = rows.at(row);
                for (int slot = 0; slot < slotsPerRow; ++slot) {
                    const size_t flat =
                        (static_cast<size_t>(row) * static_cast<size_t>(slotsPerRow)) + static_cast<size_t>(slot);
                    if (scores[flat] < m_options.recognitionThreshold) {
                        continue;
                    }
                    const std::optional<Candidate> candidate = mapCharacter(static_cast<char32_t>(codes[flat]),
                                                                            charBoxes + (flat * 4),
                                                                            scores[flat],
                                                                            item.crop.box,
                                                                            item.crop.effectiveWidth,
                                                                            item.crop.effectiveHeight,
                                                                            false);
                    if (candidate) {
                        candidates[item.boxIndex].append(*candidate);
                    }
                }
            }
        }

        // Vertical recognition. One detection box can produce several segments, and their
        // candidates are pooled under the same box index so the interval NMS removes the
        // duplicates the 64 px overlap produces.
        struct VerticalItem
        {
            qsizetype boxIndex;
            VerticalSegment segment;
        };

        QList<VerticalItem> verticalItems;
        for (const qsizetype index : std::as_const(verticalBoxes)) {
            for (const VerticalSegment &segment : verticalSegments(boxes.at(index))) {
                verticalItems.append(VerticalItem{.boxIndex = index, .segment = segment});
            }
        }
        const size_t verticalStride = static_cast<size_t>(3) * kVerticalHeight * kVerticalWidth;
        for (qsizetype start = 0; start < verticalItems.size(); start += kMaxBatchSize) {
            const qsizetype count = std::min<qsizetype>(kMaxBatchSize, verticalItems.size() - start);
            sessions.recognitionTensor.resize(static_cast<size_t>(count) * verticalStride);
            qsizetype packed = 0;
            QList<VerticalItem> rows;
            for (qsizetype offset = 0; offset < count; ++offset) {
                const VerticalItem &item = verticalItems.at(start + offset);
                float *destination = sessions.recognitionTensor.data() + (static_cast<size_t>(packed) * verticalStride);
                if (!packVerticalTensor(rgb, item.segment, destination)) {
                    continue;
                }
                rows.append(item);
                ++packed;
            }
            if (packed == 0) {
                continue;
            }
            const std::vector<int64_t> imageShape = {packed, 3, kVerticalHeight, kVerticalWidth};
            sessions.targetSizes.clear();
            for (qsizetype row = 0; row < packed; ++row) {
                sessions.targetSizes.push_back(kVerticalWidth);
                sessions.targetSizes.push_back(kVerticalHeight);
            }
            const std::vector<int64_t> rowShape = {packed, 2};
            std::vector<Ort::Value> inputs;
            inputs.push_back(Ort::Value::CreateTensor<float>(memory,
                                                             sessions.recognitionTensor.data(),
                                                             static_cast<size_t>(packed) * verticalStride,
                                                             imageShape.data(),
                                                             imageShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                memory, sessions.targetSizes.data(), sessions.targetSizes.size(), rowShape.data(), rowShape.size()));
            const std::vector<Ort::Value> outputs = sessions.vertical->Run(Ort::RunOptions{nullptr},
                                                                           sessions.verticalIo.inputs.data(),
                                                                           inputs.data(),
                                                                           inputs.size(),
                                                                           sessions.verticalIo.outputs.data(),
                                                                           sessions.verticalIo.outputs.size());
            const auto *codes = outputs.at(0).GetTensorData<int32_t>();
            const auto *charBoxes = outputs.at(1).GetTensorData<float>();
            const auto *scores = outputs.at(2).GetTensorData<float>();
            const auto shape = outputs.at(0).GetTensorTypeAndShapeInfo().GetShape();
            const int slotsPerRow = shape.size() >= 2 ? static_cast<int>(shape.at(1)) : 0;
            for (qsizetype row = 0; row < rows.size(); ++row) {
                const VerticalItem &item = rows.at(row);
                for (int slot = 0; slot < slotsPerRow; ++slot) {
                    const size_t flat =
                        (static_cast<size_t>(row) * static_cast<size_t>(slotsPerRow)) + static_cast<size_t>(slot);
                    if (scores[flat] < m_options.recognitionThreshold) {
                        continue;
                    }
                    const std::optional<Candidate> candidate = mapCharacter(static_cast<char32_t>(codes[flat]),
                                                                            charBoxes + (flat * 4),
                                                                            scores[flat],
                                                                            item.segment.box,
                                                                            item.segment.effectiveWidth,
                                                                            item.segment.effectiveHeight,
                                                                            true);
                    if (candidate) {
                        candidates[item.boxIndex].append(*candidate);
                    }
                }
            }
        }

        // Detection order is top to bottom, and the lines are reported in it.
        for (qsizetype index = 0; index < boxes.size(); ++index) {
            const auto found = candidates.constFind(index);
            if (found == candidates.constEnd()) {
                continue;
            }
            const bool vertical = boxes.at(index).height() > boxes.at(index).width();
            TextLine line = buildTextLine(*found, vertical, m_options.punctuationFactor, kOverlapThreshold);
            if (m_options.applyCorrections) {
                (void)applyMeikiCorrections(line);
            }
            if (!line.chars.isEmpty()) {
                result.lines.append(line);
            }
        }
        result.success = true;
    } catch (const std::exception &error) {
        result.success = false;
        result.lines.clear();
        result.errorMessage = QString::fromUtf8(error.what());
        qCWarning(logMeikiOcr) << "recognition failed:" << result.errorMessage;
    }

    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace maru::ocr

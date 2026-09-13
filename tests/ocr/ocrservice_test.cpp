// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "core/enums.h"
#include "fakes.h"
#include "ocr/backend.h"
#include "ocr/ocrservice.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QImage>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>

#include <atomic>
#include <gtest/gtest.h>

using namespace maru;
using namespace maru::ocr;

namespace
{

// Use the shared backend double so the OCR and scan suites model the same contract.
using maru::test::FakeBackend;

// The three-character line and the 10x10 boxes this suite asserts on, which the shared default
// ("日本語を読む" in 20x20 boxes) differs from.
std::unique_ptr<FakeBackend> makeBackend()
{
    auto backend = std::make_unique<FakeBackend>();
    backend->text = QStringLiteral("日本語");
    backend->charSize = QSize{10, 10};
    return backend;
}

void pump(const std::function<bool()> &done, int timeoutMs = 5000)
{
    const QDeadlineTimer deadline{timeoutMs};
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

QImage image(int side)
{
    QImage result{side, side, QImage::Format_RGB888};
    result.fill(Qt::white);
    return result;
}

} // namespace

TEST(OcrService, deliversTheResultOnTheCallingThreadAndGroupsTheLines)
{
    OcrService service;
    auto backend = makeBackend();
    FakeBackend *fake = backend.get();
    service.setBackend(std::move(backend));
    EXPECT_EQ(service.activeBackendName(), QStringLiteral("fake"));
    EXPECT_TRUE(service.isReady());

    Result delivered;
    QThread *callbackThread = nullptr;
    bool done = false;
    service.recognize(image(40), [&](Result result) {
        delivered = std::move(result);
        callbackThread = QThread::currentThread();
        done = true;
    });
    pump([&done] {
        return done;
    });

    ASSERT_TRUE(done) << "the service never delivered a result";
    EXPECT_TRUE(delivered.success);
    EXPECT_EQ(delivered.backendName, QStringLiteral("fake"));
    EXPECT_EQ(callbackThread, QThread::currentThread());
    // Recognition ran off the calling thread.
    EXPECT_NE(fake->workerThread.load(), QThread::currentThread());
    // The service groups the reported lines into paragraphs.
    ASSERT_EQ(delivered.paragraphs.size(), 1);
    EXPECT_EQ(delivered.paragraphs.constFirst().text, QStringLiteral("日本語"));
}

TEST(OcrService, aNewerRequestReplacesTheQueuedOne)
{
    OcrService service;
    auto backend = makeBackend();
    FakeBackend *fake = backend.get();
    fake->blocking = true;
    service.setBackend(std::move(backend));

    QList<int> deliveredSides;
    const auto record = [&deliveredSides](const Result &result) {
        deliveredSides.append(result.sourceSize.width());
    };

    service.recognize(image(10), record);
    // The first request is inside recognize() before the other two are submitted, so both of
    // them land in the one-slot queue and the second is overwritten by the third.
    ASSERT_TRUE(fake->entered.tryAcquire(1, 5000));
    service.recognize(image(20), record);
    service.recognize(image(30), record);
    fake->released.release(1);

    ASSERT_TRUE(fake->entered.tryAcquire(1, 5000));
    fake->released.release(1);
    pump([&deliveredSides] {
        return deliveredSides.size() >= 2;
    });

    EXPECT_EQ(fake->calls.load(), 2);
    EXPECT_EQ(deliveredSides, QList<int>({10, 30}));
}

TEST(OcrService, switchingTheEngineReplacesTheBackend)
{
    OcrService service;
    QSignalSpy backendSpy(&service, &OcrService::backendChanged);
    QSignalSpy availabilitySpy(&service, &OcrService::availabilityChanged);

    service.setBackend(makeBackend());
    ASSERT_EQ(backendSpy.size(), 1);
    EXPECT_EQ(backendSpy.constFirst().constFirst().toString(), QStringLiteral("fake"));
    ASSERT_EQ(availabilitySpy.size(), 1);
    EXPECT_TRUE(availabilitySpy.constFirst().constFirst().toBool());

    // The test home holds neither the meikiocr models nor a Screen AI component, so the engine
    // resolves to no backend and the injected one is dropped.
    service.setEngine(OcrEngine::Automatic);
    pump([&backendSpy] {
        return backendSpy.size() >= 2;
    });
    ASSERT_EQ(backendSpy.size(), 2);
    EXPECT_TRUE(backendSpy.at(1).constFirst().toString().isEmpty());
    EXPECT_FALSE(service.isReady());
    EXPECT_EQ(service.engine(), OcrEngine::Automatic);
}

TEST(OcrService, reportsAFailureWhenNoBackendLoaded)
{
    OcrService service;
    service.setEngine(OcrEngine::MeikiOcr);
    // The worker reports no backend and no signal changes value, so the configuration call is
    // given a fixed window to reach the worker thread and back.
    pump(
        [] {
            return false;
        },
        500);
    EXPECT_FALSE(service.isReady());

    Result delivered;
    bool done = false;
    service.recognize(image(20), [&](Result result) {
        delivered = std::move(result);
        done = true;
    });
    pump([&done] {
        return done;
    });
    ASSERT_TRUE(done);
    EXPECT_FALSE(delivered.success);
    EXPECT_FALSE(delivered.errorMessage.isEmpty());
    EXPECT_TRUE(delivered.paragraphs.isEmpty());
}

TEST(OcrService, namesTheBackendAnEngineResolvesTo)
{
    // The test home holds neither component, so every engine resolves to nothing.
    EXPECT_TRUE(OcrService::availableBackends().isEmpty());
    EXPECT_TRUE(OcrService::backendNameFor(OcrEngine::Automatic).isEmpty());
    EXPECT_TRUE(OcrService::backendNameFor(OcrEngine::MeikiOcr).isEmpty());
    EXPECT_TRUE(OcrService::backendNameFor(OcrEngine::ScreenAi).isEmpty());
}

TEST(OcrService, aDestroyedServiceDropsThePendingCallback)
{
    bool called = false;
    {
        OcrService service;
        service.setBackend(makeBackend());
        service.recognize(image(20), [&called](const Result &) {
            called = true;
        });
    }
    // The destructor stops the worker and invalidates the generation, so the queued callback
    // does nothing when the event loop reaches it.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    EXPECT_FALSE(called);
}

int main(int argc, char **argv)
{
    QCoreApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

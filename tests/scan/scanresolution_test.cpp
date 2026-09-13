// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The reported defect, reproduced without a recognition model: the popup showed the entry for the
// character under the pointer, replaced it once or twice while the pointer stood still, and came
// to rest on the entry for a neighbouring character.
//
// The cause is the progressive scan ladder. MeikiOcrBackend fits every region into a 960x544
// detector input, so the 480x270 rung reaches the detector at scale 2.000 and the 1600x900 rung at
// scale 0.600. A 14 px character therefore arrives as 28.0 detector pixels on the first rung and
// 8.4 on the last, and the detector loses characters below 24.0. Each rung runs its
// own hit test at the same pointer position and its own lookup, so each lost character is one more
// entry the popup shows.
//
// DetectorBackend below is that behaviour and nothing else: it reports every character of the page
// that falls inside the crop, and drops one character of a line that reaches
// ocr::minimumCharExtent() below the threshold. No font, no model and no fixture image is
// involved, so the suite runs on every host. tests/ocr/detectorresolution_test.cpp is the same
// measurement against the real models and a rendered page, and skips where the models are absent.
//
// The geometry is the production geometry divided by 1.5, so an 800x800 offscreen workspace holds
// the whole ladder: a 320x180 initial region growing to 640x360 and then to 800x600, over
// characters of 12 px, of 14 px and of 24 px.
#include "capture/framesource.h"
#include "capture/scanregion.h"
#include "core/settings.h"
#include "cropframesource.h"
#include "fakes.h"
#include "ocr/backend.h"
#include "ocr/meikipreprocess.h"
#include "ocr/ocrservice.h"
#include "ocr/resolution.h"
#include "scan/scancontroller.h"

#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>

#include <atomic>
#include <functional>
#include <gtest/gtest.h>
#include <memory>

using namespace maru;
using namespace maru::scan;
using maru::test::FakeTracker;

namespace
{

// The three rungs of the ladder this suite drives, and the detector input the fake backend
// reports. 960x544 is MeikiOcrBackend's own, so ocr::minimumCharExtent() answers the 23.9
// detector pixels the production path is measured against.
const QSize kInitialSize{320, 180};
const QSize kMaxSize{800, 600};
const QSize kDetectorInput{960, 544};
const QSize kSecondRung{640, 360};

// A character of 12 px reaches the detector at 36.0 px over 320x180 and at 18.0 px over 640x360,
// so the first rung resolves it and the second does not.
constexpr int kSmallCellPx = 12;
// A character of 24 px reaches the detector at 72.0 px over 320x180, 36.0 px over 640x360 and
// 21.8 px over 800x600, so the ladder climbs one rung and stops.
constexpr int kLargeCellPx = 24;
// A character of 14 px reaches the detector at 42.0 px over 320x180 and 21.0 px over 640x360. Two
// lines of it outnumber one line of 24 px characters inside a 640x360 region, which pulls
// ocr::medianCharExtent() of that region under the threshold while the region it grew out of
// reports 24 px.
constexpr int kMediumCellPx = 14;

// The index into a page line that a pass over an unresolvable line loses, and the character the
// pointer comes to rest on. The reported case is that pairing: the pointer rested on the one
// character the coarser pass dropped, and the hit test then answered its left neighbour.
constexpr int kUnresolvedIndex = 20;

QRect workspace()
{
    return capture::workspaceRect();
}

// One horizontal line of distinct characters filling the workspace width from x = 0, so the line
// reaches the left and the right edge of every rung of the ladder and capture::touchesEdge() asks
// for the next rung at each of them.
struct Line
{
    int cellPx = kSmallCellPx;
    int y = 0;
    QString text;

    [[nodiscard]] QRect boxOf(int index) const
    {
        return QRect{index * cellPx, y, cellPx, cellPx};
    }
};

// Read by DetectorBackend::recognize() on the marupop-ocr thread and assigned by setPage() on the
// main thread. The two never overlap: every case assigns before it constructs its Harness, and
// ocr::OcrService::~OcrService() joins the worker before the next case runs.
QList<Line> &page()
{
    static QList<Line> current;
    return current;
}

// One line of cellPx characters across the workspace at y. U+4E00 upwards, which jp::isJapanese()
// accepts and which gives every index its own code point, so an assertion names the character it
// expects rather than a position.
Line lineOf(int cellPx, int y, char16_t firstCodePoint)
{
    const int columns = (workspace().width() / cellPx) + 1;
    QString text;
    text.reserve(columns);
    for (int index = 0; index < columns; ++index) {
        text.append(QChar{static_cast<char16_t>(firstCodePoint + index)});
    }
    return Line{.cellPx = cellPx, .y = y, .text = text};
}

// One line across the middle of the workspace, which is the shape the reported case has.
void setPage(int cellPx)
{
    page() = QList<Line>{lineOf(cellPx, workspace().center().y(), 0x4E00)};
}

// A 24 px line the pointer rests on, plus two 14 px lines below it. The 14 px lines fall outside a
// 320x180 region centred on the 24 px line and inside a 640x360 one, and they carry 2 x 58
// characters against the 24 px line's 34 inside that region, so the median of the larger region is
// 14 px where the median of the smaller one is 24 px.
void setMixedPage()
{
    const int top = workspace().center().y();
    page() = QList<Line>{lineOf(kLargeCellPx, top, 0x4E00),
                         lineOf(kMediumCellPx, top + 120, 0x5000),
                         lineOf(kMediumCellPx, top + 160, 0x5200)};
}

// A detector of a fixed input extent. Every character of the page inside the crop is reported with
// its own box, except the one at kUnresolvedIndex of a line whose characters reach that input
// below ocr::minimumCharExtent(kDetectorInput). The surrounding boxes keep their positions, which
// is what makes ocr::extendedCharBox() of the preceding character cover the gap and the hit test
// answer that character for a pointer that has not moved.
//
// This is a second double for ocr::Backend beside maru::test::FakeBackend, and the reason is the
// coordinate space: FakeBackend lays its boxes out from an origin the case supplies and reports
// the same text for every image, so the character under the pointer is the same at every rung of
// the ladder. The defect under test is the character under the pointer changing between rungs.
class DetectorBackend : public ocr::Backend
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("detector");
    }

    bool initialize() override
    {
        m_ready = true;
        return true;
    }

    [[nodiscard]] bool isReady() const override
    {
        return m_ready;
    }

    ocr::Result recognize(const QImage &image) override
    {
        ++calls;
        const QPoint origin = test::cropOriginOf(image);
        const QRect frame{QPoint{0, 0}, image.size()};

        ocr::Result result;
        result.success = true;
        result.backendName = name();
        result.sourceSize = image.size();
        result.modelInputSize = kDetectorInput;

        for (const Line &source : page()) {
            const bool resolved = resolves(source.cellPx, image.size());
            ocr::TextLine line;
            for (int index = 0; index < source.text.size(); ++index) {
                const QRect box = source.boxOf(index).translated(-origin);
                if (!box.intersects(frame)) {
                    continue;
                }
                if (index == kUnresolvedIndex && !resolved) {
                    ++dropped;
                    continue;
                }
                const QRect clipped = box.intersected(frame);
                line.text.append(source.text.at(index));
                line.chars.append(ocr::CharBox{.codePoint = static_cast<char32_t>(source.text.at(index).unicode()),
                                               .box = clipped,
                                               .confidence = 1.0F});
                line.box = line.box.united(clipped);
            }
            if (!line.text.isEmpty()) {
                line.confidence = 1.0F;
                result.lines.append(line);
            }
        }
        return result;
    }

    std::atomic<int> calls{0};
    // Recognition passes that lost a character at kUnresolvedIndex.
    std::atomic<int> dropped{0};

private:
    // The rule ocr::resolvesCharactersAt() applies to one line, through the same
    // ocr::detectionLetterbox() the production path fits with, so the double and the gate cannot
    // drift apart. The backend answers it before it has produced the paragraphs that function
    // reads, which is why it takes the cell size rather than a result.
    [[nodiscard]] static bool resolves(int cellPx, QSize sourceSize)
    {
        return cellPx * ocr::detectionLetterbox(sourceSize, kDetectorInput).scale >=
               ocr::minimumCharExtent(kDetectorInput);
    }

    bool m_ready = false;
};

// The character under the pointer and nothing more, so an assertion on the popup content reads the
// hit test's answer directly.
lookup::Response cannedResponse(const lookup::Request &request)
{
    lookup::Result result;
    result.matchedText = request.sourceText.mid(request.cursorIndex, 1);
    result.primarySpelling = result.matchedText;

    lookup::Response response;
    response.highlightStart = request.cursorIndex;
    response.highlightLength = 1;
    response.results.append(result);
    return response;
}

void pump(const std::function<bool()> &done, int timeoutMs = 5000)
{
    const QDeadlineTimer deadline{timeoutMs};
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

void settle(int ms = 250)
{
    pump(
        [] {
            return false;
        },
        ms);
}

void configureScanning(bool poll = false, int pollMs = 60)
{
    PopSettings::setTriggerOnCursorMove(true);
    PopSettings::setCursorMoveThrottleMs(50);
    PopSettings::setPeriodicPollEnabled(poll);
    PopSettings::setPeriodicPollIntervalMs(pollMs);
    PopSettings::setProgressiveScanArea(true);
    PopSettings::setPauseWhileLocked(true);
    PopSettings::setInitialScanWidth(kInitialSize.width());
    PopSettings::setInitialScanHeight(kInitialSize.height());
    PopSettings::setMaxScanWidth(kMaxSize.width());
    PopSettings::setMaxScanHeight(kMaxSize.height());
}

struct Harness
{
    Harness()
    {
        auto owned = std::make_unique<DetectorBackend>();
        backend = owned.get();
        ocrService.setBackend(std::move(owned));

        controller = std::make_unique<ScanController>(
            tracker, frames, ocrService, [counter = lookupCount](const lookup::Request &request) {
                counter->fetch_add(1);
                return cannedResponse(request);
            });
        QObject::connect(controller.get(),
                         &ScanController::lookupReady,
                         controller.get(),
                         [this](const lookup::Response &response, const HitContext &) {
                             entries.append(response.results.constFirst().primarySpelling);
                         });
        QObject::connect(controller.get(), &ScanController::nothingUnderCursor, controller.get(), [this](QPoint) {
            ++misses;
        });
        controller->setScanning(true);
    }

    // One pointer sample, then the whole pipeline the sample starts: the throttle's trailing fire,
    // the grab, the recognition pass, the lookup and every rung the ladder adds.
    void moveTo(QPoint cursor, int settleMs = 250)
    {
        tracker.move(cursor);
        settle(settleMs);
    }

    // The distinct entries the popup was handed, in first-shown order. The reported defect is a
    // size above 1 for a pointer that came to rest on one character.
    [[nodiscard]] QStringList distinctEntries() const
    {
        QStringList distinct;
        for (const QString &entry : entries) {
            if (!distinct.contains(entry)) {
                distinct.append(entry);
            }
        }
        return distinct;
    }

    [[nodiscard]] QList<QSize> issuedSizes() const
    {
        QList<QSize> sizes;
        for (const QRect &rect : frames.issued) {
            if (!sizes.contains(rect.size())) {
                sizes.append(rect.size());
            }
        }
        return sizes;
    }

    // How often two consecutive grabs came from regions of different sizes, which counts the rungs
    // the ladder moved between. A ladder that settles reports one per rung it climbed.
    [[nodiscard]] int rungChanges() const
    {
        int changes = 0;
        for (qsizetype index = 1; index < frames.issued.size(); ++index) {
            if (frames.issued.at(index).size() != frames.issued.at(index - 1).size()) {
                ++changes;
            }
        }
        return changes;
    }

    FakeTracker tracker;
    test::CropFrameSource frames;
    ocr::OcrService ocrService;
    DetectorBackend *backend = nullptr;
    std::unique_ptr<ScanController> controller;

    QStringList entries;
    int misses = 0;
    // Counted through a shared_ptr rather than through the Harness: the callable runs on a global
    // thread pool thread, and ScanController::~ScanController() releases the controller before the
    // Harness members it would otherwise reach.
    std::shared_ptr<std::atomic<int>> lookupCount = std::make_shared<std::atomic<int>>(0);
};

} // namespace

// The reported case. The pointer crosses into the line and comes to rest on the one character a
// coarser pass loses; the popup is handed one entry, and it is that character's.
TEST(ScanResolutionTest, holdsOneEntryWhileThePointerRestsOnSmallText)
{
    configureScanning();
    setPage(kSmallCellPx);
    Harness harness;

    // In from above the line, which is the approach the report describes: the pointer starts
    // outside every character box and stops inside one.
    const QPoint rest = page().constFirst().boxOf(kUnresolvedIndex).center();
    harness.moveTo(rest - QPoint{0, 4 * kSmallCellPx});
    harness.moveTo(rest);

    EXPECT_EQ(harness.distinctEntries(), QStringList{QString{page().constFirst().text.at(kUnresolvedIndex)}});
    EXPECT_EQ(harness.backend->dropped.load(), 0);
    // The ladder never left the rung that resolves the characters, so no pass ran over 640x360.
    EXPECT_EQ(harness.issuedSizes(), QList<QSize>{kInitialSize});
}

// The counterpart: characters large enough that the second rung still resolves them. The ladder
// has to climb, or a word running past the edge of the first region is never matched whole.
TEST(ScanResolutionTest, growsWhereTheLargerRegionResolvesTheText)
{
    configureScanning();
    setPage(kLargeCellPx);
    Harness harness;

    harness.moveTo(page().constFirst().boxOf(kUnresolvedIndex).center());

    const QList<QSize> sizes = harness.issuedSizes();
    EXPECT_TRUE(sizes.contains(kInitialSize)) << sizes.size();
    EXPECT_TRUE(sizes.contains(kSecondRung)) << sizes.size();
    // 24 px reaches the detector at 21.8 px over 800x600, under the 23.9 px it separates
    // characters at, so the third rung is declined.
    EXPECT_FALSE(sizes.contains(kMaxSize)) << sizes.size();
    EXPECT_EQ(harness.backend->dropped.load(), 0);
    EXPECT_EQ(harness.distinctEntries(), QStringList{QString{page().constFirst().text.at(kUnresolvedIndex)}});
}

// The region reaches a rung that cannot resolve the text by growing over pixels holding none,
// which is the one path the growth gate cannot see coming. The next pointer sample over a
// character is what brings the region back to the initial size.
TEST(ScanResolutionTest, returnsToTheInitialRegionAfterGrowingOverEmptyPixels)
{
    configureScanning();
    setPage(kSmallCellPx);
    Harness harness;

    // Far enough above the line that a 320x180 region centred on the pointer holds no character
    // and a 640x360 one holds the whole line.
    const QPoint rest = page().constFirst().boxOf(kUnresolvedIndex).center();
    harness.moveTo(rest - QPoint{0, 140});
    ASSERT_GT(harness.misses, 0);
    ASSERT_TRUE(harness.issuedSizes().contains(kSecondRung));

    harness.moveTo(rest);

    EXPECT_EQ(harness.entries.constLast(), QString{page().constFirst().text.at(kUnresolvedIndex)});
    // The grab that answered the second position was a fresh one at the initial size rather than
    // the cached 640x360 result.
    EXPECT_EQ(harness.frames.issued.constLast().size(), kInitialSize);
}

// The growth gate predicts from the characters of the region it grows out of, and the descent
// measures the characters of the region it grew into. A larger region that brings smaller text
// into the frame lowers the median between the two, so the two rules disagree about the same
// growth; the ladder has to settle on one rung rather than alternate for the rest of the session.
TEST(ScanResolutionTest, settlesOnOneRungWhereTheLargerRegionLowersTheMedianExtent)
{
    // The poll drives the case as well as the throttle, because the pointer relay reports nothing
    // while the pointer is at rest and the poll is then the only thing that re-grabs.
    configureScanning(true, 60);
    setMixedPage();
    Harness harness;

    const Line &target = page().constFirst();
    harness.moveTo(target.boxOf(kUnresolvedIndex).center(), 800);

    const QList<QSize> sizes = harness.issuedSizes();
    ASSERT_TRUE(sizes.contains(kInitialSize)) << sizes.size();
    ASSERT_TRUE(sizes.contains(kSecondRung)) << sizes.size();
    // One climb from 320x180 to 640x360 and nothing after it. Alternating between the two rungs
    // reports one change per grab over the whole 800 ms, which is 13 at the 60 ms poll interval.
    EXPECT_LE(harness.rungChanges(), 1) << "the ladder moved between rungs " << harness.rungChanges() << " times over "
                                        << harness.frames.issued.size() << " grabs";
    EXPECT_EQ(harness.frames.issued.constLast().size(), kSecondRung);
    // The character under the pointer is 24 px and the second rung resolves it, so one lookup
    // answers every grab of the settle window.
    EXPECT_LE(harness.lookupCount->load(), 2) << harness.lookupCount->load();
    EXPECT_EQ(harness.distinctEntries(), QStringList{QString{target.text.at(kUnresolvedIndex)}});
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

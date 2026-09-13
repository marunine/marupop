// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/meikipreprocess.h"

#include "core/logging.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace maru::ocr
{

namespace
{

// Python's round() is half-to-even, and so is std::nearbyint under the default FE_TONEAREST
// rounding mode. std::lround would round half away from zero and would place a segment
// boundary one pixel away from the reference implementation on an exact .5.
int roundHalfEven(double value)
{
    return static_cast<int>(std::nearbyint(value));
}

// A view on the QImage rows. QImage pads every row to a multiple of 4 bytes, so bytesPerLine()
// has to be passed: a cv::Mat built without the step shears every image whose width is not a
// multiple of 4.
cv::Mat wrap(const QImage &rgb)
{
    return {rgb.height(),
            rgb.width(),
            CV_8UC3,
            const_cast<uchar *>(rgb.constBits()), // NOLINT(cppcoreguidelines-pro-type-const-cast)
            static_cast<size_t>(rgb.bytesPerLine())};
}

// CHW, divided by 255, into a canvas of canvasWidth x canvasHeight that is zeroed first. The
// resized content occupies the top-left corner; the remainder stays 0, which is the black
// padding the models were trained with.
void packChw(const cv::Mat &resized, int canvasWidth, int canvasHeight, float *tensor)
{
    const size_t plane = static_cast<size_t>(canvasWidth) * static_cast<size_t>(canvasHeight);
    std::fill_n(tensor, plane * 3, 0.0F);
    const int rows = std::min(resized.rows, canvasHeight);
    const int columns = std::min(resized.cols, canvasWidth);
    for (int y = 0; y < rows; ++y) {
        const auto *row = resized.ptr<uchar>(y);
        const size_t offset = static_cast<size_t>(y) * static_cast<size_t>(canvasWidth);
        for (int x = 0; x < columns; ++x) {
            tensor[offset + static_cast<size_t>(x)] = static_cast<float>(row[(3 * x) + 0]) * (1.0F / 255.0F);
            tensor[plane + offset + static_cast<size_t>(x)] = static_cast<float>(row[(3 * x) + 1]) * (1.0F / 255.0F);
            tensor[(2 * plane) + offset + static_cast<size_t>(x)] =
                static_cast<float>(row[(3 * x) + 2]) * (1.0F / 255.0F);
        }
    }
}

// cv::resize reports errors by throwing cv::Exception, which is why this translation unit is
// compiled with -fexceptions and catches at this one boundary.
bool resizeInto(const cv::Mat &source, cv::Mat &destination, int width, int height)
{
    if (width < 1 || height < 1 || source.empty()) {
        return false;
    }
    try {
        cv::resize(source, destination, cv::Size{width, height}, 0, 0, cv::INTER_LINEAR);
    } catch (const std::exception &error) {
        qCWarning(logMeikiOcr) << "cv::resize failed:" << error.what();
        return false;
    }
    return true;
}

} // namespace

Letterbox detectionLetterbox(QSize source, QSize target)
{
    Letterbox letterbox;
    if (source.width() <= 0 || source.height() <= 0) {
        return letterbox;
    }
    letterbox.scale = std::min(static_cast<double>(target.width()) / source.width(),
                               static_cast<double>(target.height()) / source.height());
    letterbox.width = static_cast<int>(source.width() * letterbox.scale);
    letterbox.height = static_cast<int>(source.height() * letterbox.scale);
    return letterbox;
}

HorizontalCrop horizontalCrop(QRect box)
{
    HorizontalCrop crop;
    crop.box = box;
    if (box.width() <= 0 || box.height() <= 0) {
        return crop;
    }
    int newHeight = kRecognitionHeight;
    const double scale = static_cast<double>(kRecognitionHeight) / box.height();
    int newWidth = roundHalfEven(box.width() * scale);
    if (newWidth > kRecognitionWidth) {
        const double widthScale = static_cast<double>(kRecognitionWidth) / newWidth;
        newWidth = kRecognitionWidth;
        newHeight = roundHalfEven(kRecognitionHeight * widthScale);
    }
    crop.effectiveWidth = newWidth;
    crop.effectiveHeight = newHeight;
    return crop;
}

QList<VerticalSegment> verticalSegments(QRect box)
{
    QList<VerticalSegment> segments;
    if (box.width() <= 0 || box.height() <= 0) {
        return segments;
    }
    // QRect::right() is inclusive; the detection boxes are half-open [x1,x2) intervals, so the
    // exclusive edges are taken from x() + width() and y() + height().
    const int y1 = box.y();
    const int y2 = box.y() + box.height();
    const double scale = static_cast<double>(kVerticalWidth) / box.width();
    const double scaledHeight = box.height() * scale;

    QList<double> starts;
    double segmentHeight = 0.0;
    int maxContentHeight = kVerticalHeight;
    if (scaledHeight > kVerticalHeight) {
        maxContentHeight = kVerticalMaxContentHeight;
        segmentHeight = kVerticalMaxContentHeight / scale;
        const double stride = (kVerticalMaxContentHeight - kVerticalOverlapPx) / scale;
        double current = y1;
        while (current + segmentHeight < y2) {
            starts.append(current);
            current += stride;
        }
        // The trailing segment is anchored at the bottom edge. The 1.0 px guard drops it where
        // it would repeat the last start.
        const double last = y2 - segmentHeight;
        if (starts.isEmpty() || last > starts.constLast() + 1.0) {
            starts.append(last);
        }
    } else {
        starts.append(y1);
        segmentHeight = y2 - y1;
    }

    for (qsizetype index = 0; index < starts.size(); ++index) {
        const double start = starts.at(index);
        const int top = roundHalfEven(start);
        const int bottom = std::min(roundHalfEven(start + segmentHeight), y2);
        const int height = bottom - top;
        if (height <= 0) {
            continue;
        }
        VerticalSegment segment;
        segment.box = QRect{box.x(), top, box.width(), height};
        segment.effectiveWidth = kVerticalWidth;
        segment.effectiveHeight = std::min(roundHalfEven(height * scale), maxContentHeight);
        segment.segmentIndex = static_cast<int>(index);
        segments.append(segment);
    }
    return segments;
}

bool packDetectionTensor(const QImage &rgb, QSize target, const Letterbox &letterbox, float *tensor)
{
    if (rgb.format() != QImage::Format_RGB888 || letterbox.width < 1 || letterbox.height < 1) {
        return false;
    }
    cv::Mat resized;
    if (!resizeInto(wrap(rgb), resized, letterbox.width, letterbox.height)) {
        return false;
    }
    packChw(resized, target.width(), target.height(), tensor);
    return true;
}

bool packHorizontalTensor(const QImage &rgb, const HorizontalCrop &crop, float *tensor)
{
    if (rgb.format() != QImage::Format_RGB888 || crop.effectiveWidth < 1 || crop.effectiveHeight < 1) {
        return false;
    }
    const QRect box = crop.box.intersected(QRect{QPoint{0, 0}, rgb.size()});
    if (box.isEmpty()) {
        return false;
    }
    const cv::Mat source = wrap(rgb)(cv::Rect{box.x(), box.y(), box.width(), box.height()});
    cv::Mat resized;
    if (!resizeInto(source, resized, crop.effectiveWidth, crop.effectiveHeight)) {
        return false;
    }
    packChw(resized, kRecognitionWidth, kRecognitionHeight, tensor);
    return true;
}

bool packVerticalTensor(const QImage &rgb, const VerticalSegment &segment, float *tensor)
{
    if (rgb.format() != QImage::Format_RGB888 || segment.effectiveHeight < 1) {
        return false;
    }
    const QRect box = segment.box.intersected(QRect{QPoint{0, 0}, rgb.size()});
    if (box.isEmpty()) {
        return false;
    }
    const cv::Mat source = wrap(rgb)(cv::Rect{box.x(), box.y(), box.width(), box.height()});
    cv::Mat resized;
    if (!resizeInto(source, resized, kVerticalWidth, segment.effectiveHeight)) {
        return false;
    }
    packChw(resized, kVerticalWidth, kVerticalHeight, tensor);
    return true;
}

QImage toRgb888(const QImage &image)
{
    if (image.format() == QImage::Format_RGB888) {
        return image;
    }
    return image.convertToFormat(QImage::Format_RGB888);
}

} // namespace maru::ocr

// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Input tensors for the three meikiocr models, ported from meikiocr/ocr.py (Apache-2.0,
// https://github.com/rtr46/meikiocr), functions _preprocess_for_detection and
// _preprocess_for_recognition. The geometry is separated from the pixel packing so the
// geometry is testable without an image. Constants and rounding points follow the
// pinned upstream implementation; see NOTICE.
//
// Every resize runs through cv::resize with cv::INTER_LINEAR, which is the operator the models
// were trained against; QImage::scaled uses a different pixel-centre convention.
#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>

namespace maru::ocr
{

// Model input extents. The batch dimension is dynamic in all three graphs; the spatial
// dimensions are baked into the exported models and cannot be changed at run time.
inline constexpr int kDetectionWidth = 960;
inline constexpr int kDetectionHeight = 544;
inline constexpr int kSmallDetectionWidth = 320;
inline constexpr int kSmallDetectionHeight = 192;
inline constexpr int kRecognitionWidth = 960;
inline constexpr int kRecognitionHeight = 32;
inline constexpr int kVerticalWidth = 32;
inline constexpr int kVerticalHeight = 480;

// Height of a vertical segment when the line is too tall for one pass, in scaled pixels. The
// value is below kVerticalHeight so every split segment carries bottom padding.
inline constexpr int kVerticalMaxContentHeight = 420;
// Overlap between two consecutive vertical segments, in scaled pixels.
inline constexpr int kVerticalOverlapPx = 64;

// Fixed output slot counts: the detection head always emits 64 boxes, the horizontal
// recognition head 48 characters, the vertical head 24 characters per segment.
inline constexpr int kMaxDetections = 64;
inline constexpr int kMaxHorizontalChars = 48;
inline constexpr int kMaxVerticalChars = 24;

// Recognition batch size, matching meikiocr's chunking convention.
inline constexpr int kMaxBatchSize = 8;

// The aspect-preserving fit of a source image into the detection input. scale is not clamped
// to 1.0, so a scan region smaller than the model input is upscaled, which is what makes a
// smaller scan region resolve smaller text.
struct Letterbox
{
    double scale = 1.0;
    int width = 0;  // truncated, not rounded: int(w_orig * scale)
    int height = 0; // truncated, not rounded: int(h_orig * scale)
};

[[nodiscard]] Letterbox detectionLetterbox(QSize source, QSize target);

// One horizontal detection box scaled to a height of 32 px, with the content extent inside the
// 32x960 input. effectiveWidth is capped at 960, which shrinks effectiveHeight below 32.
struct HorizontalCrop
{
    QRect box;
    int effectiveWidth = 0;
    int effectiveHeight = 0;
};

[[nodiscard]] HorizontalCrop horizontalCrop(QRect box);

// One vertical detection box, split into segments where the box scaled to a width of 32 px
// exceeds 480 px in height. Segments of one box overlap by kVerticalOverlapPx scaled pixels
// and their character candidates are pooled before the interval NMS, which is what removes the
// duplicates the overlap produces.
struct VerticalSegment
{
    QRect box;
    int effectiveWidth = kVerticalWidth;
    int effectiveHeight = 0;
    int segmentIndex = 0;
};

[[nodiscard]] QList<VerticalSegment> verticalSegments(QRect box);

// Writes one letterboxed detection tensor, CHW, RGB, divided by 255. tensor holds
// 3 * target.height() * target.width() floats and is overwritten in full, padding included.
// False means the resize was rejected, which happens where the letterbox extent rounds to
// zero on either axis.
[[nodiscard]] bool packDetectionTensor(const QImage &rgb, QSize target, const Letterbox &letterbox, float *tensor);

// Writes one recognition tensor, CHW, RGB, divided by 255, into 3 * 32 * 960 floats.
[[nodiscard]] bool packHorizontalTensor(const QImage &rgb, const HorizontalCrop &crop, float *tensor);

// Writes one vertical recognition tensor, CHW, RGB, divided by 255, into 3 * 480 * 32 floats.
[[nodiscard]] bool packVerticalTensor(const QImage &rgb, const VerticalSegment &segment, float *tensor);

// The image in QImage::Format_RGB888, converted where it arrives in another format. The
// preprocessing path expects RGB channel order.
[[nodiscard]] QImage toRgb888(const QImage &image);

} // namespace maru::ocr

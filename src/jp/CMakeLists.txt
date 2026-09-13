# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# japanesetables.h is generated from JL.Core/Japanese/JapaneseUtils.cs by tools/gen-jp-tables.py.
# It is checked in rather than generated at build time because the generator reads a JL checkout,
# which is not a build dependency.
target_sources(marupop_lib PRIVATE japanese.cpp japanese.h japanesetables.h)

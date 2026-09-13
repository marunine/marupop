# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The lookup engine: candidate generation, per-dictionary query, ranking, decoration and the
# adapter to the popup's view model. lookuptypes.h is the contract dict/, popup/ and scan/
# compile against.
target_sources(
    marupop_lib
    PRIVATE decorate.cpp
            decorate.h
            engine.cpp
            engine.h
            format.cpp
            format.h
            lookuptypes.h
            ocrvariants.cpp
            ocrvariants.h
            popupadapter.cpp
            popupadapter.h
            query.cpp
            query.h
            ranking.cpp
            ranking.h
            similarkanjitable.h
            textinfo.cpp
            textinfo.h)

# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The dictionary subsystem: record shapes, the CBOR payload codec, the SQLite store and its
# key-filter sidecar, the format importers, the dictionary list and the download and import jobs.
target_sources(marupop_lib PRIVATE
    codec.cpp
    codec.h
    dictionary.cpp
    dictionary.h
    dictionarydownloadjob.cpp
    dictionarydownloadjob.h
    dictionaryimportjob.cpp
    dictionaryimportjob.h
    dictionarymanager.cpp
    dictionarymanager.h
    dicttypes.h
    keyfilter.cpp
    keyfilter.h
    keynorm.cpp
    keynorm.h
    lookupsupport.cpp
    lookupsupport.h
    records.cpp
    records.h
    store.cpp
    store.h
    updatecheckjob.cpp
    updatecheckjob.h
    wordclasses.cpp
    wordclasses.h
    importers/customnameimporter.cpp
    importers/customnameimporter.h
    importers/customwordimporter.cpp
    importers/customwordimporter.h
    importers/idsimporter.cpp
    importers/idsimporter.h
    importers/importer.cpp
    importers/importer.h
    importers/jmdictimporter.cpp
    importers/jmdictimporter.h
    importers/jmnedictimporter.cpp
    importers/jmnedictimporter.h
    importers/kanjidicimporter.cpp
    importers/kanjidicimporter.h
    importers/structuredcontent.cpp
    importers/structuredcontent.h
    importers/yomitanimporter.cpp
    importers/yomitanimporter.h)

# sqlite3 drives the read path through its C API, xxhash produces the key-filter hashes and the
# import-time payload dedup keys, KF6::Archive reads a gzipped EDRDG dump and a Yomitan zip, and
# Qt6::Network downloads a built-in dictionary. None of them appears in a header this module
# installs, so all four stay off marupop_lib's public link interface.
target_link_libraries(marupop_lib PRIVATE
    PkgConfig::SQLITE3
    PkgConfig::XXHASH
    KF6::Archive
    KF6::CoreAddons
    Qt6::Network)

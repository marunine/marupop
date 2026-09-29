// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include <winver.h>

VS_VERSION_INFO VERSIONINFO
FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
PRODUCTVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "marunine"
            VALUE "FileDescription", "MaruPop"
            VALUE "FileVersion", "@PROJECT_VERSION@"
            VALUE "InternalName", "marupop"
            VALUE "LegalCopyright", "Copyright 2026 marunine. LGPL-3.0-only."
            VALUE "OriginalFilename", "marupop.exe"
            VALUE "ProductName", "MaruPop"
            VALUE "ProductVersion", "@PROJECT_VERSION@"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END

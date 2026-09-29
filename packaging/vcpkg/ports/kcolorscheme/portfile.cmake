# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Overlay port for a KDE framework missing from the vcpkg registry at the vcpkg.json baseline. The
# port structure follows the baseline kconfig port.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KDE/kcolorscheme
    REF "v${VERSION}"
    SHA512 2ffa1d80599fcb1098f89e5c022ad11794ce4b490f418904b978cbbd3587f7e8ab85a2bcaa4798e696dc45f10ac4c628285fb44c050d2b51f991bb82036c85ad
    HEAD_REF master
)

# A .clang-format in the source tree stops KDEClangFormat from writing one during configure. The
# KDEClangFormat write blocks parallel configure runs.
file(WRITE "${SOURCE_PATH}/.clang-format" "DisableFormat: true\nSortIncludes: false\n")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DBUILD_PYTHON_BINDINGS=OFF
        -DKF_SKIP_PO_PROCESSING=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME kf6colorscheme CONFIG_PATH lib/cmake/KF6ColorScheme)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(GLOB LICENSE_FILES "${SOURCE_PATH}/LICENSES/*")
vcpkg_install_copyright(FILE_LIST ${LICENSE_FILES})

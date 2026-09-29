# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Overlay port for a KDE framework missing from the vcpkg registry at the vcpkg.json baseline. The
# port structure follows the baseline kconfig port.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KDE/kconfigwidgets
    REF "v${VERSION}"
    SHA512 3bf2b50486c1dca1085a0e6c2bc2c78ef55bbf4d0c5d38fda2b65e6209a5c8ad4b0bd3c4e0dfb0f4e51dafad61639874ef7b8be4fb8b9ce875d929898d258885
    HEAD_REF master
)

# A .clang-format in the source tree stops KDEClangFormat from writing one during configure. The
# KDEClangFormat write blocks parallel configure runs.
file(WRITE "${SOURCE_PATH}/.clang-format" "DisableFormat: true\nSortIncludes: false\n")

# The upstream USE_DBUS default is OFF outside Linux. The explicit USE_DBUS=OFF keeps Qt6DBus out
# of the port dependencies on every host. The Designer plugin requires qttools, which the port
# dependencies omit.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DBUILD_DESIGNERPLUGIN=OFF
        -DBUILD_PYTHON_BINDINGS=OFF
        -DUSE_DBUS=OFF
        -DKF_SKIP_PO_PROCESSING=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME kf6configwidgets CONFIG_PATH lib/cmake/KF6ConfigWidgets)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(GLOB LICENSE_FILES "${SOURCE_PATH}/LICENSES/*")
vcpkg_install_copyright(FILE_LIST ${LICENSE_FILES})

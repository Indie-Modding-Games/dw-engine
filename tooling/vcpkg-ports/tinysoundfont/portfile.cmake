# TinySoundFont is one header (tsf.h) without a CMake package and is not in the vcpkg baseline, so
# the repository pins it here to a commit and its SHA-512. Only the header and the licence are
# installed; the engine compiles the implementation once (engine/audio/src/tsf.cpp).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO schellingb/TinySoundFont
    REF 853a0a171759f1ddba0de1442133a75912bbeffa
    SHA512 f7b19442ff86fc8f766cac6890b41ef60d60aa57e384e99b75d102b782803f0092dbb2b279ceaedd1a3bfc6378f2253bb7dfaca03af47d63f363783dca38b09b
    HEAD_REF main
)

file(INSTALL "${SOURCE_PATH}/tsf.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

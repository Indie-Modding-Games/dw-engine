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

# The synthesizer reads but drops each zone's reverb send; the engine sends the voices that have it
# to the sound chip's reverb (docs/architecture/audio.md, "Reverberación"), so the header keeps the
# generator in its region. It is stored as read, never applied by the synthesizer itself.
vcpkg_replace_string("${SOURCE_PATH}/tsf.h"
    "int freqVibLFO, vibLfoToPitch;"
    "int freqVibLFO, vibLfoToPitch;\n\tint reverbEffectsSend;")
vcpkg_replace_string("${SOURCE_PATH}/tsf.h"
    "{ 0                                , (0                                                  ) }, //16 ReverbEffectsSend (unsupported)"
    "{ GEN_INT                          , _TSFREGIONOFFSET(         int, reverbEffectsSend    ) }, //16 ReverbEffectsSend (stored)")
file(INSTALL "${SOURCE_PATH}/tsf.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

dwe_add_module(formats
    LAYER base
    SOURCES
        src/disc/boot.cpp
        src/disc/cue_sheet.cpp
        src/disc/disc_image.cpp
        src/disc/file_table.cpp
        src/disc/iso9660.cpp
        src/disc/known_releases.cpp
        src/disc/sector.cpp
        src/exe/psx_exe.cpp
        src/image/tim.cpp
        src/image/tim_packs.cpp
    DEPS core)

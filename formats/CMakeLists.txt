dwe_add_module(formats
    LAYER base
    SOURCES
        src/disc/cue_sheet.cpp
        src/disc/disc_image.cpp
        src/disc/known_releases.cpp
        src/disc/sector.cpp
    DEPS core)

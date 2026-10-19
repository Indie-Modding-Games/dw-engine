# The tool's code is a module so its tests can link it; the command line is a thin app on top.
dwe_add_module(dwx
    LAYER apps
    SOURCES
        src/extract.cpp
        src/verify.cpp
    DEPS core formats)

dwe_add_app(dwx_cli
    LAYER apps
    SOURCES
        src/main.cpp
    DEPS dwx
    OUTPUT_NAME dwx)

dwe_add_module(net
    LAYER base
    SOURCES
        src/endpoint.cpp
        src/lockstep.cpp
        src/loopback.cpp
        src/reliable_channel.cpp
        src/udp.cpp
        src/wire.cpp
    DEPS core)

# The UDP link (udp.hpp) needs Winsock on Windows.
if(WIN32)
    target_link_libraries(dwe_net PRIVATE ws2_32)
endif()

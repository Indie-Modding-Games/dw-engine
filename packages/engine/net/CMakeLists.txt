find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

dwe_add_module(net
    LAYER base
    SOURCES
        src/channel_peer_link.cpp
        src/lockstep.cpp
        src/loopback.cpp
        src/reliable_channel.cpp
        src/rtc_peer_link.cpp
        src/signal.cpp
        src/trusted_roots.cpp
        src/wire.cpp
    DEPS core
    # WebRTC data channels and the signaling server's WebSocket (MPL-2.0: tooling/vcpkg-ports/
    # libdatachannel builds it with Mbed TLS and lets its C API take the trusted CA certificates).
    PRIVATE_LIBS LibDataChannel::LibDataChannel nlohmann_json::nlohmann_json
    TEST_LIBS LibDataChannel::LibDataChannel nlohmann_json::nlohmann_json)

# The system's root certificates (src/trusted_roots.cpp).
if(WIN32)
    target_link_libraries(dwe_net PRIVATE crypt32)
endif()

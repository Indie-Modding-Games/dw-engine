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
        src/wire.cpp
    DEPS core
    # WebRTC data channels and the signaling server's WebSocket (MPL-2.0, used unmodified:
    # tooling/vcpkg-ports/libdatachannel only builds it with Mbed TLS).
    PRIVATE_LIBS LibDataChannel::LibDataChannel nlohmann_json::nlohmann_json
    TEST_LIBS nlohmann_json::nlohmann_json)

add_library(ilegacysim_network STATIC
    apple80211_hle.cpp
    darwin_network_abi.cpp
    darwin_route_socket.cpp
    host_network.cpp
    wifi_state.cpp)
add_library(iLegacySim::network ALIAS ilegacysim_network)
ilegacysim_configure_module(ilegacysim_network)
target_link_libraries(ilegacysim_network PUBLIC iLegacySim::foundation)

qt_add_library(network_client ClientTransport.h LocalClientTransport.h LocalClientTransport.cpp TcpClientTransport.h TcpClientTransport.cpp TlsClientTransport.h TlsClientTransport.cpp)

target_include_directories(network_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/communication)

target_link_libraries(network_client PRIVATE Qt6::Core Qt6::Network messages)

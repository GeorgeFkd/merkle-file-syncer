qt_add_library(network_server ServerTransport.h LocalServerTransport.h LocalServerTransport.cpp TcpServerTransport.h TcpServerTransport.cpp TlsServerTransport.h TlsServerTransport.cpp)

target_include_directories(network_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/communication)

target_link_libraries(network_server PRIVATE Qt6::Core Qt6::Network messages)

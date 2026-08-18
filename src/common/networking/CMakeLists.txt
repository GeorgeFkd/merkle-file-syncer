qt_add_library(network_client ClientTransport.h LocalClientTransport.h LocalClientTransport.cpp TcpClientTransport.h TcpClientTransport.cpp)

target_include_directories(network_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)

target_link_libraries(network_client PRIVATE Qt6::Core Qt6::Network messages)


qt_add_library(network_server ServerTransport.h LocalServerTransport.h LocalServerTransport.cpp TcpServerTransport.h TcpServerTransport.cpp)

target_include_directories(network_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)

target_link_libraries(network_server PRIVATE Qt6::Core Qt6::Network messages)

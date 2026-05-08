qt_add_library(merkle_server_lib STATIC FileServer.cpp)
target_include_directories(
  merkle_server_lib PUBLIC ${CMAKE_SOURCE_DIR}/src/common/communication
                           ${CMAKE_SOURCE_DIR}/src/common/storage)
target_link_libraries(merkle_server_lib PRIVATE messages filesutil storage
                                                Qt6::Core Qt6::Network)
qt_add_executable(merkle_server main.cpp)
target_link_libraries(merkle_server PRIVATE merkle_server_lib Qt6::Network)

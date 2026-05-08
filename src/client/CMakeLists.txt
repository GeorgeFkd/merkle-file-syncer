qt_add_library(merkle_client_lib STATIC FileClient.cpp FileClient.h)
target_include_directories(
  merkle_client_lib PUBLIC ${CMAKE_SOURCE_DIR}/src/common/communication
                           ${CMAKE_SOURCE_DIR}/src/common/storage)
target_link_libraries(merkle_client_lib PRIVATE messages filesutil storage
                                                Qt6::Core Qt6::Network)
qt_add_executable(merkle_client main.cpp)

target_link_libraries(merkle_client PRIVATE Qt6::Core Qt6::Network
                                            merkle_client_lib)

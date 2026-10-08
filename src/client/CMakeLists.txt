qt_add_library(merkle_client_lib STATIC
  FileClient.cpp
  FileClient.h
)
target_include_directories(
  merkle_client_lib
  PUBLIC ${CMAKE_SOURCE_DIR}/src/subsystems/communication
         ${CMAKE_SOURCE_DIR}/src/subsystems/storage
         ${CMAKE_SOURCE_DIR}/src/subsystems/filesystem
         ${CMAKE_SOURCE_DIR}/src/subsystems/sync_protocol
         ${CMAKE_SOURCE_DIR}/src/subsystems/sync_protocol/client
         ${CMAKE_SOURCE_DIR}/src/subsystems/auth/client
         ${CMAKE_SOURCE_DIR}/src/subsystems/fs_metadata
         ${CMAKE_SOURCE_DIR}/src/subsystems/hashing
         ${CMAKE_SOURCE_DIR}/src/subsystems/file_transfer/client
         ${CMAKE_SOURCE_DIR}/src/subsystems/networking/client
         ${CMAKE_SOURCE_DIR}/src/subsystems/fs_scanner
         )
target_link_libraries(
  merkle_client_lib PRIVATE messages hashing fs_metadata filesutil storage fs_scanner Qt6::Core merkle_protocol_client naive_protocol_client PUBLIC auth_client PUBLIC file_transfer_client
                             Qt6::Network network_client)
qt_add_executable(merkle_client main.cpp)

target_link_libraries(merkle_client PRIVATE Qt6::Core Qt6::Network
                                            merkle_client_lib)

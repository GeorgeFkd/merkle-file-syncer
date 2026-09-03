qt_add_library(merkle_client_lib STATIC 
  FileClient.cpp 
  FileClient.h 
)
target_include_directories(
  merkle_client_lib
  PUBLIC ${CMAKE_SOURCE_DIR}/src/common/communication
         ${CMAKE_SOURCE_DIR}/src/common/storage
         ${CMAKE_SOURCE_DIR}/src/common/filesystem
         ${CMAKE_SOURCE_DIR}/src/common/sync_protocol
         ${CMAKE_SOURCE_DIR}/src/common/auth
         ${CMAKE_SOURCE_DIR}/src/common/fs_metadata
         ${CMAKE_SOURCE_DIR}/src/common/hashing
         ${CMAKE_SOURCE_DIR}/src/common/file_transfer
         ${CMAKE_SOURCE_DIR}/src/common/networking
         )
target_link_libraries(
  merkle_client_lib PRIVATE messages hashing fs_metadata filesutil storage Qt6::Core merkle_protocol_client naive_protocol_client PUBLIC auth_client PUBLIC file_transfer_client
                             Qt6::Network network_client)
qt_add_executable(merkle_client main.cpp)

target_link_libraries(merkle_client PRIVATE Qt6::Core Qt6::Network
                                            merkle_client_lib)

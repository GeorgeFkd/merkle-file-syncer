qt_add_library(merkle_client_lib STATIC 
  FileClient.cpp 
  FileClient.h 
  ClientTransport.h 
  LocalClientTransport.h 
  LocalClientTransport.cpp
  TcpClientTransport.h
  TcpClientTransport.cpp
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
         )
target_link_libraries(
  merkle_client_lib PRIVATE messages hashing fs_metadata filesutil storage users_db Qt6::Core merkle_protocol_client naive_protocol_client
                            Qt6::Network)
qt_add_executable(merkle_client main.cpp)

target_link_libraries(merkle_client PRIVATE Qt6::Core Qt6::Network
                                            merkle_client_lib)

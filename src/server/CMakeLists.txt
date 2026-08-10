qt_add_library(merkle_server_lib STATIC 
  FileServer.cpp 
  LocalServerTransport.h 
  LocalServerTransport.cpp 
  ServerTransport.h 
  TcpServerTransport.h 
  TcpServerTransport.cpp)

target_include_directories(
  merkle_server_lib
  PRIVATE ${CMAKE_SOURCE_DIR}/src/common/communication
         ${CMAKE_SOURCE_DIR}/src/common/storage
         ${CMAKE_SOURCE_DIR}/src/common/filesystem
         ${CMAKE_SOURCE_DIR}/src/common/auth
         ${CMAKE_SOURCE_DIR}/src/common/sync_protocol
         ${CMAKE_SOURCE_DIR}/src/common/fs_metadata
         ${CMAKE_SOURCE_DIR}/src/common/hashing
         ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  merkle_server_lib
  PUBLIC messages
          hashing
          filesutil
          storage
          sessionregistry
          users_db
          fs_metadata
          Qt6::Core
          Qt6::Network
merkle_protocol_server
naive_protocol_server)
set_target_properties(merkle_server_lib PROPERTIES AUTOMOC ON)

# qt_add_executable(merkle_server main.cpp)
# target_link_libraries(merkle_server PRIVATE merkle_server_lib Qt6::Network)

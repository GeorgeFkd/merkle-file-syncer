qt_add_library(merkle_server_lib STATIC 
  FileServer.cpp 
  # LocalServerTransport.h 
  # LocalServerTransport.cpp 
  # ServerTransport.h 
  # TcpServerTransport.h 
  # TcpServerTransport.cpp
)

target_include_directories(
  merkle_server_lib
  PRIVATE ${CMAKE_SOURCE_DIR}/src/subsystems/communication
         ${CMAKE_SOURCE_DIR}/src/subsystems/storage
         ${CMAKE_SOURCE_DIR}/src/subsystems/filesystem
         ${CMAKE_SOURCE_DIR}/src/subsystems/auth/server
         ${CMAKE_SOURCE_DIR}/src/subsystems/sync_protocol
         ${CMAKE_SOURCE_DIR}/src/subsystems/sync_protocol/server
         ${CMAKE_SOURCE_DIR}/src/subsystems/fs_metadata
         ${CMAKE_SOURCE_DIR}/src/subsystems/hashing
         ${CMAKE_SOURCE_DIR}/src/subsystems/file_transfer/server
         ${CMAKE_SOURCE_DIR}/src/subsystems/networking/server
         ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  merkle_server_lib
  PUBLIC messages
          hashing
          network_server
          filesutil
          storage
          sessionregistry
          users_db
          auth_server
          fs_metadata
          Qt6::Core
          Qt6::Network
merkle_protocol_server
naive_protocol_server
file_transfer_server
)
set_target_properties(merkle_server_lib PROPERTIES AUTOMOC ON)

# qt_add_executable(merkle_server main.cpp)
# target_link_libraries(merkle_server PRIVATE merkle_server_lib Qt6::Network)

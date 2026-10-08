qt_add_library(file_transfer_server FileTransferServer.cpp FileTransferServer.h)
target_include_directories(file_transfer_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/chunking_protocol/server)
target_link_libraries(file_transfer_server PUBLIC
  Qt6::Core
  chunking_server
  chunking_common
  storage        # same storage target
  messages)

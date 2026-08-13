qt_add_library(file_transfer_client FileTransferClient.cpp FileTransferClient.h)
target_include_directories(file_transfer_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/chunking_protocol)
target_link_libraries(file_transfer_client PUBLIC
  Qt6::Core
  chunking_client
  chunking_common
  storage        # whatever target FileStorage/LocalFileStorage/S3FileStorage live in
  messages)

qt_add_library(file_transfer_server FileTransferServer.cpp FileTransferServer.h)
target_include_directories(file_transfer_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}  ${CMAKE_SOURCE_DIR}/src/common/chunking_protocol)
target_link_libraries(file_transfer_server PUBLIC
  Qt6::Core
  chunking_server
  chunking_common
  storage        # same storage target
  messages)

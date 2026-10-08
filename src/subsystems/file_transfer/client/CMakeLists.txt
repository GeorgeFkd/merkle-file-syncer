qt_add_library(file_transfer_client FileTransferClient.cpp FileTransferClient.h)
target_include_directories(file_transfer_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/chunking_protocol/client)
target_link_libraries(file_transfer_client PUBLIC
  Qt6::Core
  chunking_client
  chunking_common
  storage        # whatever target FileStorage/LocalFileStorage/S3FileStorage live in
  messages)

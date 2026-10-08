qt_add_library(naive_protocol_server NaiveSyncServer.cpp NaiveSyncServer.h)
target_include_directories(naive_protocol_server PUBLIC
${CMAKE_SOURCE_DIR}/src/subsystems/fs_metadata
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/src/subsystems/communication)
target_link_libraries(naive_protocol_server PUBLIC messages Qt6::Core fs_metadata)


qt_add_library(merkle_protocol_server MerkleSyncServer.cpp MerkleSyncServer.h)
target_include_directories(merkle_protocol_server PUBLIC
${CMAKE_SOURCE_DIR}/src/subsystems/filesystem
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/src/subsystems/communication)
target_link_libraries(merkle_protocol_server filesutil messages Qt6::Core sync_common)


qt_add_library(naive_protocol_client NaiveSyncClient.cpp NaiveSyncClient.h)
target_include_directories(naive_protocol_client PUBLIC
${CMAKE_SOURCE_DIR}/src/subsystems/fs_metadata
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/src/subsystems/communication)
target_link_libraries(naive_protocol_client PUBLIC messages Qt6::Core fs_metadata sync_common)

qt_add_library(merkle_protocol_client MerkleSyncClient.cpp MerkleSyncClient.h)
target_include_directories(merkle_protocol_client PUBLIC
${CMAKE_SOURCE_DIR}/src/subsystems/filesystem
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/src/subsystems/communication)
target_link_libraries(merkle_protocol_client filesutil messages Qt6::Core sync_common)


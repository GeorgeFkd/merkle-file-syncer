add_library(sync_common SyncCommon.cpp SyncCommon.h)
target_include_directories(sync_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sync_common PRIVATE Qt6::Core)


qt_add_library(naive_protocol_client NaiveSyncClient.cpp NaiveSyncClient.h)
target_include_directories(naive_protocol_client PUBLIC ${CMAKE_SOURCE_DIR}/src/common/fs_metadata ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(naive_protocol_client PUBLIC messages Qt6::Core fs_metadata sync_common)

qt_add_library(naive_protocol_server NaiveSyncServer.cpp NaiveSyncServer.h)
target_include_directories(naive_protocol_server PUBLIC ${CMAKE_SOURCE_DIR}/src/common/fs_metadata ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(naive_protocol_server PUBLIC messages Qt6::Core fs_metadata)



qt_add_library(merkle_protocol_client MerkleSyncClient.cpp MerkleSyncClient.h)
target_include_directories(merkle_protocol_client PUBLIC ${CMAKE_SOURCE_DIR}/src/common/filesystem ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(merkle_protocol_client filesutil messages Qt6::Core sync_common)

qt_add_library(merkle_protocol_server MerkleSyncServer.cpp MerkleSyncServer.h)
target_include_directories(merkle_protocol_server PUBLIC ${CMAKE_SOURCE_DIR}/src/common/filesystem ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(merkle_protocol_server filesutil messages Qt6::Core)

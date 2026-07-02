

qt_add_library(merkle_protocol_client MerkleSyncClient.cpp MerkleSyncClient.h)
target_include_directories(merkle_protocol_client PRIVATE ${CMAKE_SOURCE_DIR}/src/common/filesystem ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(merkle_protocol_client filesutil messages Qt6::Core)

qt_add_library(merkle_protocol_server MerkleSyncServer.cpp MerkleSyncServer.h)
target_include_directories(merkle_protocol_server PRIVATE ${CMAKE_SOURCE_DIR}/src/common/filesystem ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(merkle_protocol_server filesutil messages Qt6::Core)

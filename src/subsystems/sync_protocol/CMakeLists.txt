add_library(sync_common SyncCommon.cpp SyncCommon.h)
target_include_directories(sync_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sync_common PRIVATE Qt6::Core)

add_subdirectory(client)
add_subdirectory(server)

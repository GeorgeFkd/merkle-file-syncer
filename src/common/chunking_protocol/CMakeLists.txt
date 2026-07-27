qt_add_library(chunking_client ChunkingClient.cpp ChunkingClient.h)
target_include_directories(chunking_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_client PRIVATE Qt6::Core)

qt_add_library(chunking_server ChunkingServer.cpp ChunkingServer.h)
target_include_directories(chunking_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_server PRIVATE Qt6::Core)

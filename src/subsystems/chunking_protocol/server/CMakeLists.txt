qt_add_library(chunking_server ChunkingServer.cpp ChunkingServer.h)
target_include_directories(chunking_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_server PUBLIC Qt6::Core chunking_common)

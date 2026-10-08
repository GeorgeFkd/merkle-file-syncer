qt_add_library(chunking_client ChunkingClient.cpp ChunkingClient.h)
target_include_directories(chunking_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_client PUBLIC Qt6::Core chunking_common)

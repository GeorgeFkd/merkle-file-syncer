add_library(chunking_common ChunkingProtocolMessages.cpp)
target_include_directories(chunking_common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_common PRIVATE Qt6::Core)

qt_add_library(chunking_client ChunkingClient.cpp ChunkingClient.h)
target_include_directories(chunking_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_client PRIVATE Qt6::Core chunking_common)

qt_add_library(chunking_server ChunkingServer.cpp ChunkingServer.h)
target_include_directories(chunking_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_server PRIVATE Qt6::Core chunking_common)

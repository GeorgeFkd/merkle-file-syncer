add_library(chunking_common ChunkingProtocolMessages.cpp)
target_include_directories(chunking_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication ${CMAKE_SOURCE_DIR}/src/common/hashing)
target_link_libraries(chunking_common PUBLIC messages hashing Qt6::Core)

qt_add_library(chunking_client ChunkingClient.cpp ChunkingClient.h)
target_include_directories(chunking_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_client PUBLIC Qt6::Core chunking_common)

qt_add_library(chunking_server ChunkingServer.cpp ChunkingServer.h)
target_include_directories(chunking_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chunking_server PUBLIC Qt6::Core chunking_common)

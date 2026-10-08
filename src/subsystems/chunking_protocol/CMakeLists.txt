add_library(chunking_common ChunkingProtocolMessages.cpp)
target_include_directories(chunking_common PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/src/subsystems/communication
${CMAKE_SOURCE_DIR}/src/subsystems/hashing)
target_link_libraries(chunking_common PUBLIC messages hashing Qt6::Core)

add_subdirectory(client)
add_subdirectory(server)

add_library(fs_metadata FSMetadata.cpp FSMetadata.h)
target_include_directories(fs_metadata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/filesystem)
target_link_libraries(fs_metadata PUBLIC filesutil Qt6::Core)

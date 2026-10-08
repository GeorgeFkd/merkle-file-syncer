add_library(fs_scanner FSScanner.cpp FSScanner.h)
target_include_directories(fs_scanner PUBLIC
${CMAKE_SOURCE_DIR}/src/subsystems/storage
)
target_link_libraries(fs_scanner PRIVATE Qt6::Core PUBLIC storage)

add_subdirectory(communication)
add_subdirectory(filesystem)
add_subdirectory(storage)

qt_add_library(filehasher FileHasher.cpp)
target_include_directories(filehasher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(filehasher PRIVATE storage Qt6::Core)

add_library(hashing Hasher.cpp Hasher.h)
target_include_directories(hashing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hashing PRIVATE Qt6::Core)

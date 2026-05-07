qt_add_library(storage FileDb.cpp FileDb.h)
target_include_directories(messages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(storage PRIVATE Qt6::Core)

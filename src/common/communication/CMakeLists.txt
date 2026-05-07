qt_add_library(messages STATIC Messages.cpp Messages.h)
target_include_directories(messages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(messages PRIVATE Qt6::Core)

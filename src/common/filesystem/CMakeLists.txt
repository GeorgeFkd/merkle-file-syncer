qt_add_library(filesutil FileTree.cpp)
target_include_directories(filesutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(filesutil PRIVATE Qt6::Core)

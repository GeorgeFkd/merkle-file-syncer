qt_add_library(merkle_common STATIC merkle.cpp)
target_include_directories(merkle_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(merkle_common PRIVATE Qt6::Core Qt6::Network Qt6::Sql)

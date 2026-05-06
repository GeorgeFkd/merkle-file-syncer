qt_add_library(merkle_common STATIC)
target_link_libraries(merkle_common PRIVATE Qt6::Core Qt6::Network Qt6::Sql)

qt_add_executable(merkle_server main.cpp)
target_link_libraries(merkle_server PRIVATE Qt6::Core Qt6::Network Qt6::Sql
                                            merkle_common)

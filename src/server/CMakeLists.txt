qt_add_executable(merkle_server main.cpp FileServer.cpp)
target_link_libraries(merkle_server PRIVATE Qt6::Core Qt6::Network Qt6::Sql
                                            messages filesutil storage)

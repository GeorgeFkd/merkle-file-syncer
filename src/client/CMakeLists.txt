qt_add_executable(merkle_client main.cpp FileClient.cpp)
target_link_libraries(merkle_client PRIVATE Qt6::Core Qt6::Network messages
                                            filesutil storage)

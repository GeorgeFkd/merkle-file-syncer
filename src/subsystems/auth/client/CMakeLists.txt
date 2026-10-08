qt_add_library(auth_client STATIC AuthClient.cpp AuthClient.h)
target_include_directories(auth_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/communication)
target_link_libraries(auth_client PRIVATE Qt6::Core PUBLIC messages)

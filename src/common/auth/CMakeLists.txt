add_library(key_provider STATIC KeyProvider.cpp KeyProvider.h)
target_include_directories(key_provider PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(key_provider PRIVATE Qt6::Core)


add_library(sessionregistry STATIC SessionRegistry.cpp SessionRegistry.h)
target_include_directories(sessionregistry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sessionregistry PUBLIC Qt6::Core)

add_library(users_db STATIC UsersDb.cpp UsersDb.h)
target_include_directories(users_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(users_db PUBLIC Qt6::Core)

qt_add_library(auth_client STATIC AuthClient.cpp AuthClient.h)
target_include_directories(auth_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(auth_client PRIVATE Qt6::Core PUBLIC users_db PUBLIC messages)

find_package(jwt-cpp CONFIG REQUIRED)
qt_add_library(auth_server STATIC AuthServer.cpp AuthServer.h)
target_include_directories(auth_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/communication)
target_link_libraries(auth_server PRIVATE Qt6::Core PUBLIC users_db PUBLIC messages sessionregistry PUBLIC jwt-cpp::jwt-cpp PRIVATE key_provider)

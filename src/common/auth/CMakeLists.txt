add_library(sessionregistry STATIC SessionRegistry.cpp SessionRegistry.h)

target_include_directories(sessionregistry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sessionregistry PUBLIC Qt6::Core)

add_library(users_db STATIC UsersDb.cpp UsersDb.h)

target_include_directories(users_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(users_db PUBLIC Qt6::Core)

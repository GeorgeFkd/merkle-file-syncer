add_library(key_provider STATIC KeyProvider.cpp KeyProvider.h)
target_include_directories(key_provider PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(key_provider PRIVATE Qt6::Core)


add_library(sessionregistry STATIC SessionRegistry.cpp SessionRegistry.h)
target_include_directories(sessionregistry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sessionregistry PUBLIC Qt6::Core)

# sodium::sodium comes from cmake/libsodium.cmake (vendored submodule build).
add_library(users_db STATIC UsersDb.cpp UsersDb.h)
target_include_directories(users_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(users_db PUBLIC Qt6::Core PRIVATE sodium::sodium)

# Argon2id cost profile for password hashing; maps onto libsodium's
# crypto_pwhash_{OPS,MEM}LIMIT_<PROFILE> constants. Production must use
# INTERACTIVE or stronger (the default). MIN only exists to make local test
# runs fast (~56ms vs ~0.1ms per hash/verify) and must be opted into
# explicitly with -DAUTH_ARGON2_PROFILE=MIN; it is independent of BUILD_TESTS
# so CI can run the tests with production values.
set(AUTH_ARGON2_PROFILE INTERACTIVE CACHE STRING
    "Argon2id cost profile for UsersDb (MIN for tests; INTERACTIVE, MODERATE or SENSITIVE for production)")
set_property(CACHE AUTH_ARGON2_PROFILE PROPERTY STRINGS MIN INTERACTIVE MODERATE SENSITIVE)
if(NOT AUTH_ARGON2_PROFILE MATCHES "^(MIN|INTERACTIVE|MODERATE|SENSITIVE)$")
  message(FATAL_ERROR "AUTH_ARGON2_PROFILE must be one of MIN, INTERACTIVE, MODERATE, SENSITIVE (got '${AUTH_ARGON2_PROFILE}')")
endif()

target_compile_definitions(users_db PRIVATE
  AUTH_ARGON2_OPSLIMIT=crypto_pwhash_OPSLIMIT_${AUTH_ARGON2_PROFILE}
  AUTH_ARGON2_MEMLIMIT=crypto_pwhash_MEMLIMIT_${AUTH_ARGON2_PROFILE}
  AUTH_ARGON2_PROFILE_NAME="${AUTH_ARGON2_PROFILE}")

if(AUTH_ARGON2_PROFILE STREQUAL "MIN")
  message(WARNING
    "UsersDb password hashing uses Argon2id profile MIN (crypto_pwhash_OPSLIMIT_MIN / MEMLIMIT_MIN). "
    "This is for tests only and is NOT safe for production. Every binary in this build tree, "
    "including the client and server, uses it. Production builds must configure with "
    "-DAUTH_ARGON2_PROFILE=INTERACTIVE (the default), or MODERATE/SENSITIVE.")
else()
  message(STATUS
    "UsersDb password hashing uses Argon2id profile ${AUTH_ARGON2_PROFILE} "
    "(production should use INTERACTIVE or stronger; MIN is for tests only)")
endif()

find_package(jwt-cpp CONFIG REQUIRED)
qt_add_library(auth_server STATIC AuthServer.cpp AuthServer.h)
target_include_directories(auth_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/communication)
target_link_libraries(auth_server PRIVATE Qt6::Core PUBLIC users_db PUBLIC messages sessionregistry PUBLIC jwt-cpp::jwt-cpp PRIVATE key_provider)

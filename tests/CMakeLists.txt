add_subdirectory(${CMAKE_SOURCE_DIR}/rapidcheck ${CMAKE_BINARY_DIR}/rapidcheck)
add_subdirectory(${CMAKE_SOURCE_DIR}/googletest ${CMAKE_BINARY_DIR}/googletest)

add_executable(merkle_tests main.cpp tests.cpp)
target_link_libraries(
  merkle_tests
  PRIVATE GTest::gtest
          rapidcheck
          merkle_server_lib
          merkle_client_lib
          filesutil
          storage
          messages
          Qt6::Core
          Qt6::Network)
target_include_directories(
  merkle_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/src/client ${CMAKE_SOURCE_DIR}/src/server
          ${CMAKE_SOURCE_DIR}/rapidcheck/extras/gtest/include)

set_target_properties(merkle_tests PROPERTIES AUTOMOC ON)

include(GoogleTest)
gtest_discover_tests(merkle_tests)

add_subdirectory(${CMAKE_SOURCE_DIR}/rapidcheck ${CMAKE_BINARY_DIR}/rapidcheck)
add_subdirectory(${CMAKE_SOURCE_DIR}/googletest ${CMAKE_BINARY_DIR}/googletest)

include(GoogleTest)

set(TEST_COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src/client ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/rapidcheck/extras/gtest/include)

add_executable(tree_tests main.cpp tree_tests.cpp)
target_link_libraries(
  tree_tests
  PRIVATE GTest::gtest
          rapidcheck
          filesutil
          filehasher
          storage
          messages
          Qt6::Core)
target_include_directories(tree_tests PRIVATE ${TEST_COMMON_INCLUDES})
set_target_properties(tree_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(tree_tests)

add_executable(sync_tests main.cpp sync_tests.cpp)
target_link_libraries(
  sync_tests
  PRIVATE GTest::gtest
          rapidcheck
          merkle_server_lib
          merkle_client_lib
          filesutil
          filehasher
          storage
          messages
          Qt6::Core
          Qt6::Network)
target_include_directories(sync_tests PRIVATE ${TEST_COMMON_INCLUDES})
if(BUILD_S3_IMPL)
  target_include_directories(sync_tests
                             PRIVATE $ENV{VCPKG_ROOT}/installed/x64-linux)
endif()
set_target_properties(sync_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(sync_tests)

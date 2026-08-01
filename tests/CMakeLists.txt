add_subdirectory(${CMAKE_SOURCE_DIR}/rapidcheck ${CMAKE_BINARY_DIR}/rapidcheck)
add_subdirectory(${CMAKE_SOURCE_DIR}/googletest ${CMAKE_BINARY_DIR}/googletest)

include(GoogleTest)

set(TEST_COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src/client ${CMAKE_SOURCE_DIR}/src/server
    # ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/rapidcheck/extras/gtest/include)

add_executable(tree_tests tree_tests.cpp)
target_link_libraries(
  tree_tests
  PRIVATE GTest::gtest_main
          rapidcheck
          filesutil
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


add_executable(storage_tests storage_tests.cpp)
target_link_libraries(
  storage_tests
  PRIVATE GTest::gtest_main
          rapidcheck
          storage
          Qt6::Core)
target_include_directories(storage_tests PRIVATE ${TEST_COMMON_INCLUDES})
if(BUILD_S3_IMPL)
  target_include_directories(storage_tests
                             PRIVATE $ENV{VCPKG_ROOT}/installed/x64-linux)
endif()
set_target_properties(storage_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(storage_tests)



add_executable(protocol_tests main.cpp protocol_tests.cpp)
target_link_libraries(
  protocol_tests 
    PRIVATE GTest::gtest
            filesutil
            merkle_protocol_client
            merkle_protocol_server
            rapidcheck
            Qt6::Core
)
target_include_directories(protocol_tests PRIVATE ${TEST_COMMON_INCLUDES}
${CMAKE_SOURCE_DIR}/src/common/filesystem
${CMAKE_SOURCE_DIR}/src/common/sync_protocol)
set_target_properties(protocol_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(protocol_tests PROPERTIES ENVIRONMENT "RC_PARAMS=max_success=500 max_size=300")

option(ENABLE_ASAN "Build tests with AddressSanitizer" OFF)

add_executable(chunking_tests main.cpp chunking_tests.cpp)

target_link_libraries(chunking_tests PRIVATE
  GTest::gtest
  rapidcheck
  chunking_client
  chunking_server
  Qt6::Core
)

target_include_directories(chunking_tests PRIVATE
  ${TEST_COMMON_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/common/chunking_protocol
)

set_target_properties(chunking_tests PROPERTIES AUTOMOC ON)

if(ENABLE_ASAN)
  target_compile_options(chunking_tests PRIVATE
    -fsanitize=address -g -O1 -fno-omit-frame-pointer)
  target_link_options(chunking_tests PRIVATE -fsanitize=address)
endif()

gtest_discover_tests(chunking_tests)

add_executable(file_transfer_tests main.cpp file_transfer_tests.cpp)
target_link_libraries(file_transfer_tests PRIVATE
  GTest::gtest
  rapidcheck
  file_transfer_client
  file_transfer_server
  storage
  messages
  Qt6::Core
)
target_include_directories(file_transfer_tests PRIVATE
  ${TEST_COMMON_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/common/file_transfer
  ${CMAKE_SOURCE_DIR}/src/common/chunking_protocol
)
if(BUILD_S3_IMPL)
  target_include_directories(file_transfer_tests
                             PRIVATE $ENV{VCPKG_ROOT}/installed/x64-linux)
endif()
set_target_properties(file_transfer_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(file_transfer_tests)

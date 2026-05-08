#[[
Libraries needed: Qt->network,cryptographic stuff,core
For testing: rapidcheck for property based testing(and catch22 or googletest)
In the common module will be the message/protocol related stuff, the merkle tree implementation
and maybe read/write abstractions depending on how far this will go
]]

cmake_minimum_required(VERSION 3.16)
project(merkle_sync)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)

qt_standard_project_setup()

add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/client)
if(BUILD_TESTS)
  message(STATUS "tests are being built")
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "tests will not be built set BUILD_TESTS to ON")
endif()

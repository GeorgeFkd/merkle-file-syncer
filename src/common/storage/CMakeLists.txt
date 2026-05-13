option(BUILD_S3_IMPL "Build S3 storage implementation" OFF)

set(STORAGE_SOURCES FileDb.cpp FileDb.h FileStorage.h LocalFileStorage.cpp
                    LocalFileStorage.h)

if(BUILD_S3_IMPL)
  list(APPEND STORAGE_SOURCES S3FileStorage.cpp S3FileStorage.h)
  list(APPEND CMAKE_PREFIX_PATH $ENV{VCPKG_ROOT}/installed/x64-linux)
  find_package(miniocpp CONFIG REQUIRED)
  message(STATUS "S3 server implementation will be built.")
endif()

qt_add_library(storage STATIC ${STORAGE_SOURCES})
target_include_directories(storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(storage PRIVATE Qt6::Core)

if(BUILD_S3_IMPL)
  target_link_libraries(storage PUBLIC miniocpp::miniocpp)
endif()

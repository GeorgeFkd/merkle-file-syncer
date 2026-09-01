# Builds the vendored libsodium submodule and exposes it as sodium::sodium.
#
# libsodium ships an autotools build (no CMakeLists.txt), so it cannot be pulled
# in with add_subdirectory(). Instead it is built once via ExternalProject into
# ${CMAKE_BINARY_DIR}/libsodium and consumed as an imported static library.
# autogen.sh is required because the git checkout has no generated ./configure;
# -s marks it as a developer setup, -b keeps it from fetching config.guess/sub
# off the network. The configure/make run happens out of tree, so the submodule
# working copy stays clean apart from the generated autotools scripts.

include(ExternalProject)
include(ProcessorCount)

set(LIBSODIUM_SOURCE_DIR ${CMAKE_SOURCE_DIR}/libsodium)

if(NOT EXISTS ${LIBSODIUM_SOURCE_DIR}/configure.ac)
  message(
    FATAL_ERROR
      "The libsodium submodule is empty. Run:\n"
      "  git submodule update --init --recursive libsodium")
endif()

set(LIBSODIUM_PREFIX ${CMAKE_BINARY_DIR}/libsodium)
set(LIBSODIUM_INSTALL_DIR ${LIBSODIUM_PREFIX}/install)
set(LIBSODIUM_INCLUDE_DIR ${LIBSODIUM_INSTALL_DIR}/include)
set(LIBSODIUM_LIBRARY
    ${LIBSODIUM_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}sodium${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ProcessorCount(LIBSODIUM_BUILD_JOBS)
if(LIBSODIUM_BUILD_JOBS EQUAL 0)
  set(LIBSODIUM_BUILD_JOBS 1)
endif()

# --libdir is pinned because autotools defaults it to lib64 on some distros,
# which would leave LIBSODIUM_LIBRARY pointing at a path that never appears.
set(LIBSODIUM_CONFIGURE_ARGS
    --prefix=${LIBSODIUM_INSTALL_DIR}
    --libdir=${LIBSODIUM_INSTALL_DIR}/lib
    --enable-static
    --disable-shared
    --with-pic
    --disable-dependency-tracking)

ExternalProject_Add(
  libsodium_external
  SOURCE_DIR ${LIBSODIUM_SOURCE_DIR}
  PREFIX ${LIBSODIUM_PREFIX}
  BINARY_DIR ${LIBSODIUM_PREFIX}/build
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir ${LIBSODIUM_SOURCE_DIR} sh
                    ./autogen.sh -s -b
  COMMAND ${LIBSODIUM_SOURCE_DIR}/configure ${LIBSODIUM_CONFIGURE_ARGS}
          CC=${CMAKE_C_COMPILER}
  BUILD_COMMAND make -j${LIBSODIUM_BUILD_JOBS}
  INSTALL_COMMAND make install
  BUILD_BYPRODUCTS ${LIBSODIUM_LIBRARY}
  LOG_CONFIGURE ON
  LOG_BUILD ON
  LOG_INSTALL ON
  LOG_OUTPUT_ON_FAILURE ON)

# The include dir only appears after the install step, but a target's
# INTERFACE_INCLUDE_DIRECTORIES must already exist at configure time.
file(MAKE_DIRECTORY ${LIBSODIUM_INCLUDE_DIR})

add_library(sodium::sodium STATIC IMPORTED GLOBAL)
set_target_properties(
  sodium::sodium
  PROPERTIES IMPORTED_LOCATION ${LIBSODIUM_LIBRARY}
             INTERFACE_INCLUDE_DIRECTORIES ${LIBSODIUM_INCLUDE_DIR})
add_dependencies(sodium::sodium libsodium_external)

qt_add_library(filesutil FileTree.cpp MerkleTree.cpp)
target_include_directories(
  filesutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/common/hashing)
target_link_libraries(filesutil PRIVATE hashing Qt6::Core)

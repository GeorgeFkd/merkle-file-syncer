qt_add_library(filesutil FileTree.cpp SimpleFileTree.cpp MerkleTree.cpp)
target_include_directories(
  filesutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                   ${CMAKE_SOURCE_DIR}/src/common/storage)
target_link_libraries(filesutil PRIVATE Qt6::Core storage)

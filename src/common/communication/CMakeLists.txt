qt_add_library(messages STATIC Messages.cpp Messages.h)
target_include_directories(
  messages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                  ${CMAKE_SOURCE_DIR}/src/common/filesystem)
target_link_libraries(messages PRIVATE Qt6::Core filesutil)
set_target_properties(messages PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(roundtrip_test roundtrip_test.cpp)
target_link_libraries(roundtrip_test PRIVATE loo_core)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
  # Drives the MCP server (which calls the built looping-cli) like a real client.
  add_test(NAME mcp_server COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/mcp/test.js)
  set_tests_properties(mcp_server PROPERTIES ENVIRONMENT "LOOPING_CLI=$<TARGET_FILE:looping-cli>")
endif()

add_executable(editor_test editor_test.cpp)
target_link_libraries(editor_test PRIVATE loo_core)
add_test(NAME editor COMMAND editor_test ${CMAKE_SOURCE_DIR}/looping_file_examples)

# Every .loo in the corpus must read and write back byte for byte.
add_test(NAME corpus_roundtrip
         COMMAND roundtrip_test ${CMAKE_SOURCE_DIR}/looping_file_examples)

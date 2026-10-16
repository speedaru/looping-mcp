# Embed the template .loo files (real files saved by Looping) into the library.
function(embed_file VAR FILE)
  file(READ ${FILE} HEX_CONTENT HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX_CONTENT}")
  set(${VAR} "${BYTES}" PARENT_SCOPE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FILE})
endfunction()

embed_file(LINK_TEMPLATE_BYTES ${CMAKE_CURRENT_SOURCE_DIR}/templates/link_base.loo)
embed_file(ATTR_TEMPLATE_BYTES ${CMAKE_CURRENT_SOURCE_DIR}/templates/attr_base.loo)
configure_file(src/template_data.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/template_data.cpp @ONLY)

add_library(loo_core STATIC
  src/archive.cpp
  src/objects.cpp
  src/document.cpp
  src/editor.cpp
  src/types.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/template_data.cpp
)
target_include_directories(loo_core PUBLIC include)
target_include_directories(loo_core PRIVATE src)
target_compile_features(loo_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(loo_core PRIVATE /W4)
else()
  target_compile_options(loo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

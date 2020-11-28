cmake_minimum_required(VERSION 3.1)

project(context-game-engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Werror -Wall -Wextra -Wno-unused-parameter -pedantic
                      -fcoroutines)
endif()

file(GLOB util_sources src/util/*.cc)
list(FILTER util_sources EXCLUDE REGEX ".*_test.cc")
add_library(util ${util_sources})
target_compile_options(util PRIVATE -fcoroutines)

file(GLOB util_tests src/util/*_test.cc)
foreach(file ${util_tests})
  get_filename_component(name ${file} NAME_WE)
  add_executable(${name} ${file})
  target_link_libraries(${name} util)
endforeach()

add_executable(engine src/engine.cc)
target_link_libraries(engine util)

add_executable(base64 src/base64.cc)
target_link_libraries(base64 util)

add_executable(sha1 src/sha1.cc)
target_link_libraries(sha1 util)

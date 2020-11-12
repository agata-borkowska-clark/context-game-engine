cmake_minimum_required(VERSION 3.1)

project(context-game-engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB util_sources src/util/*.cc)
add_library(util ${util_sources})
target_compile_options(util PRIVATE -fcoroutines)

add_executable(engine src/engine.cc)
target_link_libraries(engine util)

add_executable(base64 src/base64.cc)
target_link_libraries(base64 util)

if(MSVC)
  target_compile_options(engine PRIVATE /W4 /WX)
else()
  target_compile_options(engine PRIVATE -Wall -Wextra -pedantic -fcoroutines)
endif()

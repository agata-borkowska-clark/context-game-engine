cmake_minimum_required(VERSION 3.1)

project(context-game-engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB util_sources src/util/*.cc)
add_library(util ${util_sources})

add_executable(engine src/engine.cc)
target_link_libraries(engine util)

if(MSVC)
  target_compile_options(engine PRIVATE /W4 /WX)
else()
  target_compile_options(engine PRIVATE -Wall -Wextra -pedantic)
endif()

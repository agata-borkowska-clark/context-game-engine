cmake_minimum_required(VERSION 3.1)

project(context-game-engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(util src/util/executor.cc src/util/result.cc src/util/status.cc src/util/serial.cc)

add_executable(engine src/engine.cc)
target_link_libraries(engine util)

if(MSVC)
  target_compile_options(engine PRIVATE /W4 /WX)
else()
  target_compile_options(engine PRIVATE -Wall -Wextra -pedantic)
endif()

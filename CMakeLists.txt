cmake_minimum_required(VERSION 3.31)
project(symbols)

# Disable C++ modules scanning - this project doesn't use C++ modules, and the
# auto-injected flags (-fmodules-ts, -fmodule-mapper, -fdeps-format) are not
# understood by clangd.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

################################################################################
### fetch content
################################################################################

include(FetchContent)

# DC
FetchContent_Declare(
  dc
  GIT_REPOSITORY git@github.com:dumheter/dc.git
  GIT_TAG main
)
set(DC_ENABLE_LIB_DTEST ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(dc)

target_compile_definitions(dc PRIVATE -DDC_LOG_PREFIX_DATETIME=4 -DDC_LOG_PREFIX_LEVEL=0 -DDC_LOG_PREFIX_FUNCTION=0 -DDC_LOG_PREFIX_FILESTAMP=1)

# Tree-sitter core
# SOURCE_SUBDIR points to a non-existent path so FetchContent_MakeAvailable
# downloads the source but does NOT process the upstream CMakeLists.txt.
# We only need lib/src/lib.c + lib/include -- built as our own static target.
FetchContent_Declare(
  tree-sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG        v0.26.6
  SOURCE_SUBDIR  _no_cmake_subdir_
)
FetchContent_MakeAvailable(tree-sitter)

add_library(tree-sitter-lib STATIC
  ${tree-sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree-sitter-lib
  PRIVATE ${tree-sitter_SOURCE_DIR}/lib/src
  PUBLIC  ${tree-sitter_SOURCE_DIR}/lib/include
)
target_compile_features(tree-sitter-lib PRIVATE c_std_11)
if(MSVC)
  target_compile_options(tree-sitter-lib PRIVATE /wd4018 /wd4232 /wd4244 /wd4267 /wd4701)
endif()

# Tree-sitter C++ grammar
# SOURCE_SUBDIR points to a non-existent path so FetchContent_MakeAvailable
# downloads the source but does NOT run the grammar's own CMakeLists.txt
# (which would try to invoke the tree-sitter CLI to regenerate parser.c).
# We compile the pre-generated parser.c / scanner.c directly.
FetchContent_Declare(
  tree-sitter-cpp
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
  GIT_TAG        v0.23.4
  SOURCE_SUBDIR  _no_cmake_subdir_
)
FetchContent_MakeAvailable(tree-sitter-cpp)

add_library(tree-sitter-cpp-grammar STATIC
  ${tree-sitter-cpp_SOURCE_DIR}/src/parser.c
  ${tree-sitter-cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree-sitter-cpp-grammar
  PRIVATE ${tree-sitter-cpp_SOURCE_DIR}/src
)
target_link_libraries(tree-sitter-cpp-grammar PRIVATE tree-sitter-lib)
target_compile_features(tree-sitter-cpp-grammar PRIVATE c_std_11)

################################################################################
### main
################################################################################

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/scanner.cpp
    src/parser.cpp
    src/indexer.cpp
    src/json.cpp
    src/protocol.cpp
    src/server.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE dc tree-sitter-lib tree-sitter-cpp-grammar)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /Zc:preprocessor /W4 /WX /wd4324 /wd4201 /wd4100 /wd4505)
else ()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Werror -Wundef -Wno-gnu-zero-variadic-macro-arguments)
endif ()

################################################################################
### test
################################################################################

add_executable(test
    tests/main_test.cpp
    tests/json.test.cpp
    tests/parser.test.cpp
    tests/indexer.test.cpp
    tests/scanner.test.cpp
    tests/protocol.test.cpp
    src/scanner.cpp
    src/parser.cpp
    src/indexer.cpp
    src/json.cpp
    src/protocol.cpp
    src/server.cpp
)
target_include_directories(test PRIVATE src tests)
target_link_libraries(test PRIVATE dc dtest tree-sitter-lib tree-sitter-cpp-grammar)
target_compile_features(test PRIVATE cxx_std_23)

if (MSVC)
    target_compile_options(test PRIVATE /utf-8 /Zc:preprocessor /W4 /WX /wd4324 /wd4201 /wd4100 /wd4505)
else ()
    target_compile_options(test PRIVATE -Wall -Wextra -pedantic -Werror -Wundef -Wno-gnu-zero-variadic-macro-arguments)
endif ()

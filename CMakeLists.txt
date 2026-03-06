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

################################################################################
### main
################################################################################

add_executable(${PROJECT_NAME}
    src/main.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE dc)
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
)
target_include_directories(test PRIVATE src tests)
target_link_libraries(test PRIVATE dc dtest)
target_compile_features(test PRIVATE cxx_std_23)

if (MSVC)
    target_compile_options(test PRIVATE /utf-8 /Zc:preprocessor /W4 /WX /wd4324 /wd4201 /wd4100 /wd4505)
else ()
    target_compile_options(test PRIVATE -Wall -Wextra -pedantic -Werror -Wundef -Wno-gnu-zero-variadic-macro-arguments)
endif ()

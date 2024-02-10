cmake_minimum_required(VERSION 3.15.0)
project(cpp-json-rpc VERSION 0.2.0)

option(BUILD_EXAMPLES "Build examples" ON)

set(LIB_NAME ${PROJECT_NAME})
add_library(${LIB_NAME} INTERFACE)

add_library(cpp::json-rpc ALIAS ${LIB_NAME})

target_include_directories(${LIB_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(${LIB_NAME}
    INTERFACE
        cxx_std_20
)

if (BUILD_EXAMPLES)
    include(FetchContent)

    FetchContent_Declare(
        json 
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )

    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 10.1.1
        CMAKE_ARGS -DFMT_DOC=OFF -DFMT_INSTALL=OFF -DFMT_TEST=OFF
    )

    FetchContent_MakeAvailable(fmt json)

    add_subdirectory(examples)
endif()

include(CTest)
enable_testing()

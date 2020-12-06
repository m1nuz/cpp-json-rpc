find_package(Threads REQUIRED)

set(APP_CLIENT cpp-jrpc-client)
set(APP_SERVER cpp-jrpc-server)

add_executable(${APP_SERVER}
    jrpc-server.cpp
)

target_compile_features(${APP_SERVER}
    PUBLIC
        cxx_std_17
)

target_include_directories(${APP_SERVER}
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
        ${json_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${APP_SERVER}
    PUBLIC 
        Threads::Threads
        rt
)

add_executable(${APP_CLIENT}
    jrpc-client.cpp
)

target_compile_features(${APP_CLIENT}
    PUBLIC
        cxx_std_17
)

target_include_directories(${APP_CLIENT}
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
        ${json_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${APP_CLIENT}
    PUBLIC 
        Threads::Threads
        rt
)
find_package(Threads REQUIRED)

set(EXAMPLE_SERVER server)
add_executable(${EXAMPLE_SERVER}
    server.cpp
)

target_compile_features(${EXAMPLE_SERVER}
    PUBLIC
    	cxx_std_23
)

target_compile_options(${EXAMPLE_SERVER}
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-pedantic -Wall -Wextra -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)

target_link_libraries(${EXAMPLE_SERVER}
    PRIVATE
        cpp::json-rpc
        fmt::fmt
        nlohmann_json::nlohmann_json
        Threads::Threads
)

set(EXAMPLE_CLIENT client)
add_executable(${EXAMPLE_CLIENT}
    client.cpp
)

target_compile_features(${EXAMPLE_CLIENT}
    PUBLIC
    	cxx_std_23
)

target_compile_options(${EXAMPLE_CLIENT}
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-pedantic -Wall -Wextra -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)

target_link_libraries(${EXAMPLE_CLIENT}
    PRIVATE
        cpp::json-rpc
        fmt::fmt
        nlohmann_json::nlohmann_json
        Threads::Threads
)
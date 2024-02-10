#include <fmt/core.h>
#include <jsonrpc.hpp>

#include <csignal>
#include <thread>

#define USE_ASYNC_CALL

#ifdef USE_SHARED_MEMORY
using Client = jsonrpc::Client<jsonrpc::SharedMemoryChannel>;
#else
using Client = jsonrpc::Client<jsonrpc::TcpChannel>;
#endif // USE_SHARED_MEMORY

int main() {
    Client client;

#ifdef USE_ASYNC_CALL
    std::signal(SIGINT, [](int) { exit(0); });

    std::jthread t { [&client]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        fmt::println("Delayed call");

        client.async_call(
            "subtract", [](int result) { fmt::println("subtract Result: {}", result); }, 42, 23);

        client.async_call(
            "sqrt", [](int result) { fmt::println("sqrt Result: {}", result); }, 4);
    } };

    client.run();
#else
    fmt::println("subtract Result: {}", client.call<int>("subtract", 42, 23));
    fmt::println("sqrt Result: {}", client.call<int>("sqrt", 4));
#endif

    return 0;
}
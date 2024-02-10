#include <fmt/core.h>
#include <jsonrpc.hpp>

#include <csignal>

#ifdef USE_SHARED_MEMORY
using Server = jsonrpc::Server<jsonrpc::SharedMemoryChannel>;
#else
using Server = jsonrpc::Server<jsonrpc::TcpChannel>;
#endif // USE_SHARED_MEMORY

int main() {

    std::signal(SIGINT, [](int) { exit(0); });

    Server server;
    server.bind("subtract", [](int a, int b) { return a - b; });
    server.bind("sqrt", [](int a) { return a * a; });
    server.bind("notify_hello", [](const std::string& val) { fmt::println("{}", val); });

    server.run();

    return 0;
}
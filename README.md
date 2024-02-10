# C++ JSON-RPC Header-only Library

This lightweight C++ library offers a simple implementation of JSON-RPC for seamless communication between different components of your software system.

## Features

- **Header-only:** Simply include the provided header files in your project.
- **JSON-RPC Support:** Implement JSON-RPC methods and handle requests and responses effortlessly.
- **Easy Integration:** Integrate JSON-RPC functionality seamlessly into your existing codebase.

## Usage

Server:
```cpp
#include <fmt/core.h>
#include <jsonrpc.hpp>

#include <csignal>

using Server = jsonrpc::Server<jsonrpc::TcpChannel>;

int main() {
    std::signal(SIGINT, [](int) { exit(0); });

    Server server;
    server.bind("subtract", [](int a, int b) { return a - b; });

    server.run();

    return 0;
}
```

Client:
```cpp
#include <fmt/core.h>
#include <jsonrpc.hpp>

using Client = jsonrpc::Client<jsonrpc::TcpChannel>;

int main() {
    fmt::println("Result: {}", client.call<int>("subtract", 42, 23));

    return 0;
}

```
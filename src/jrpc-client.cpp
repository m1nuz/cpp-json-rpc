#include <iostream>
#include <jrpc/jrpc.hpp>

int main( int argc, char* argv[] ) {
    // jsonrpc::Client<jsonrpc::SharedMemoryChannel> client;
    jsonrpc::Client<jsonrpc::TcpChannel> client;
    std::cout << "Result: " << client.call<int>( 1, "subtract", 42, 23 ) << std::endl;

    return 0;
}
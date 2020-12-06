#include <jrpc/jrpc.hpp>

int main( int argc, char* argv[] ) {
    // jsonrpc::Client<jsonrpc::SharedMemoryChannel> client;
    jsonrpc::Client<jsonrpc::TcpChannel> client;
    client.call( 1, "subtract", 42, 23 );

    // while ( true ) {
    //     std::this_thread::sleep_for( std::chrono::seconds { 1 } );
    // }

    return 0;
}
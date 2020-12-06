#include <jrpc/jrpc.hpp>

#include <csignal>

using Server = jsonrpc::Server<jsonrpc::TcpChannel>;
Server server;

int main( int argc, char* argv[] ) {

    server.bind( "subtract", []( int a, int b ) { return a - b; } );
    server.bind( "notify_hello", []( const std::string& val ) { std::cout << "Hello " << val << "!" << std ::endl; } );

    std::signal( SIGINT, []( auto sig ) {
        server.cleanup( );
        exit( 0 );
    } );

    return server.run( );
}
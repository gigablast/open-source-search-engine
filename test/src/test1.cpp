#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TcpSocket.h"
#include "HttpRequest.h"

#include "Mem.h"

#include <iostream>

// ------------------------------------------
bool g_recoveryMode = false;
int g_inMemcpy=0;
int32_t g_recoveryLevel = 0;

bool sendPageSEO(TcpSocket *s, HttpRequest *hr) {
        return false; }
// ------------------------------------------

unsigned int factorial( unsigned int number ) {
    return number <= 1 ? number : factorial(number-1)*number;
}

TEST_CASE( "factorials are computed", "[factorial]" ) {
    REQUIRE( factorial(1) == 1 );
    REQUIRE( factorial(2) == 2 );
    REQUIRE( factorial(3) == 6 );
    REQUIRE( factorial(10) == 3628800 );
}

int main( int argc, char* argv[] ) {
  g_conf.m_runAsDaemon = false;
  g_conf.m_logToFile = false;
  char stackPointTestAnchor;
  g_mem.setStackPointer( &stackPointTestAnchor );
  g_mem.m_memtablesize = 2048;

  int result = Catch::Session().run( argc, argv );

  return result;
}

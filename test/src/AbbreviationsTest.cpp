#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>

#include "TcpSocket.h"
#include "HttpRequest.h"

#include "Mem.h"

#include "Abbreviations.h"


#include <iostream>
#include <thread>
#include <vector>

// ------------------------------------------
bool g_recoveryMode = false;
int g_inMemcpy=0;
int32_t g_recoveryLevel = 0;

bool sendPageSEO(TcpSocket *s, HttpRequest *hr) {
        return false; }
// ------------------------------------------

TEST_CASE( "isAbbr('lang') is an abbreviation and has no word after", "[isAbbr]" ) {
    bool hasWordAfter = true;
    REQUIRE( isAbbr( hash64Lower_utf8("lang"), &hasWordAfter ) == true );
    REQUIRE( hasWordAfter == false );
}

TEST_CASE( "isAbbr('vs') is an abbreviation and has a word after", "[isAbbr]" ) {
    bool hasWordAfter = false;
    REQUIRE( isAbbr( hash64Lower_utf8("vs"), &hasWordAfter ) == true );
    REQUIRE( hasWordAfter == true );
}

TEST_CASE( "isAbbr('xyz') is not an abbreviation", "[isAbbr]" ) {
    REQUIRE( isAbbr( hash64Lower_utf8("xyz") ) == false );
}

TEST_CASE("isAbbr('') is not an abbreviation", "[isAbbr]") {
    REQUIRE(isAbbr(hash64Lower_utf8("")) == false);
}

TEST_CASE("isAbbr('!@#$') is not an abbreviation", "[isAbbr]") {
    REQUIRE(isAbbr(hash64Lower_utf8("!@#$")) == false);
}

TEST_CASE("isAbbr is case insensitive", "[isAbbr]") {
    bool hasWordAfter = true;
    REQUIRE(isAbbr(hash64Lower_utf8("LANG"), &hasWordAfter) == true);
    REQUIRE(hasWordAfter == false);
    REQUIRE(isAbbr(hash64Lower_utf8("Lang"), &hasWordAfter) == true);
    REQUIRE(hasWordAfter == false);
}

TEST_CASE("isAbbr supports multi-threaded lookups", "[isAbbr]") {
    // Pre-initialize table
    REQUIRE(isAbbr(hash64Lower_utf8("lang")) == true);

    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::atomic<bool> success = true;

    auto lookupTask = [&success]() {
        for (int i = 0; i < 1000; ++i) {
            bool hasWordAfter;
            if (!isAbbr(hash64Lower_utf8("lang"), &hasWordAfter) || hasWordAfter != false) {
                success = false;
            }
        }
    };

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(lookupTask);
    }

    for (auto &t : threads) {
        t.join();
    }

    REQUIRE(success == true);
}

TEST_CASE("isAbbr covers abbreviations with different hasWordAfter values", "[isAbbr]") {
    bool hasWordAfter = false;
    REQUIRE(isAbbr(hash64Lower_utf8("Dr"), &hasWordAfter) == true);
    REQUIRE(hasWordAfter == false);

    REQUIRE(isAbbr(hash64Lower_utf8("vs"), &hasWordAfter) == true);
    REQUIRE(hasWordAfter == true);

    REQUIRE(isAbbr(hash64Lower_utf8("Ms"), &hasWordAfter) == true);
    REQUIRE(hasWordAfter == true);
}

TEST_CASE("isAbbr performance test", "[isAbbr]") {
    const uint64_t hashed = hash64Lower_utf8("lang");
    BENCHMARK("Run isAbbr 10000 times")
    {
        for (int i = 0; i < 10000; ++i) {
            bool hasWordAfter;
            REQUIRE(isAbbr(hashed, &hasWordAfter) == true);
        }
    };
}

TEST_CASE("isAbbr correctly handles resetAbbrTable", "[resetAbbrTable]") {
    REQUIRE(isAbbr(hash64Lower_utf8("lang")) == true);
    resetAbbrTable();
    REQUIRE(isAbbr(hash64Lower_utf8("lang")) == false);
}

int main( int argc, char* argv[] ) {
  g_conf.m_runAsDaemon = false;
  g_conf.m_logToFile = false;
  char stackPointTestAnchor;
  g_mem.setStackPointer( &stackPointTestAnchor );
  g_mem.m_memtablesize = 2048;
  hashinit();

  return Catch::Session().run( argc, argv );
}

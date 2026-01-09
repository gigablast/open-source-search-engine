#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>

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

TEST_CASE("HashTableX Basic Operations", "[HashTableX]") {
    HashTableX hashTable;

    SECTION("Initialize HashTableX") {
        REQUIRE(hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable") == true);
        REQUIRE(hashTable.getNumSlots() == 16); // HashTableX rounds to powers of 2
    }

    SECTION("Add and Retrieve Key-Value Pairs") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key1 = 123;
        int value1 = 456;
        int key2 = 789;
        int value2 = 101112;

        REQUIRE(hashTable.addKey(&key1, &value1));
        REQUIRE(hashTable.addKey(&key2, &value2));

        REQUIRE(*(int*)hashTable.getValue(&key1) == value1);
        REQUIRE(*(int*)hashTable.getValue(&key2) == value2);
    }

    SECTION("Remove Key") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key = 123;
        int value = 456;

        REQUIRE(hashTable.addKey(&key, &value));
        REQUIRE(hashTable.removeKey(&key));

        REQUIRE(hashTable.getValue(&key) == nullptr);
    }

    SECTION("Handle Duplicate Keys with Allow Duplicates false") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key = 123;
        int value1 = 456;
        int value2 = 789;

        REQUIRE(hashTable.addKey(&key, &value1));
        REQUIRE(hashTable.addKey(&key, &value2)); // Allowed because duplicates are enabled

        // The most recent value should be stored
        REQUIRE(*(int*)hashTable.getValue(&key) == value2);
    }

    SECTION("Clear HashTableX") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key = 123;
        int value = 456;

        REQUIRE(hashTable.addKey(&key, &value));
        hashTable.clear();

        REQUIRE(hashTable.getValue(&key) == nullptr);
        REQUIRE(hashTable.isTableEmpty());
    }

    SECTION("Test Collision Handling") {
        hashTable.set(4, 4, 4, nullptr, 0, false, 0, "testHashTable");

        int key1 = 1;
        int value1 = 100;
        int key2 = 5; // Assumes hash collision with key1
        int value2 = 200;

        REQUIRE(hashTable.addKey(&key1, &value1));
        REQUIRE(hashTable.addKey(&key2, &value2));

        REQUIRE(*(int*)hashTable.getValue(&key1) == value1);
        REQUIRE(*(int*)hashTable.getValue(&key2) == value2);
    }

    SECTION("Resize HashTableX") {
        hashTable.set(4, 4, 2, nullptr, 0, false, 0, "testHashTable");

        for (int i = 0; i < 100; ++i) {
            REQUIRE(hashTable.addKey(&i, &i));
        }

        REQUIRE(hashTable.getNumSlots() >= 100); // Ensure resizing occurred

        for (int i = 0; i < 100; ++i) {
            REQUIRE(*(int*)hashTable.getValue(&i) == i);
        }
    }

    SECTION("Handle Large Keys and Values") {
        hashTable.set(8, 16, 4, nullptr, 0, false, 0, "testHashTable");

        int64_t largeKey = 1234567890123456LL;
        struct LargeValue {
            char data[16];
        } largeValue = {"LargeTestData"};

        REQUIRE(hashTable.addKey(&largeKey, &largeValue));
        auto* retrievedValue = (LargeValue*)hashTable.getValue(&largeKey);

        REQUIRE(retrievedValue != nullptr);
        REQUIRE(strcmp(retrievedValue->data, "LargeTestData") == 0);
    }

    SECTION("Check Empty Table Behavior") {
        REQUIRE(hashTable.isTableEmpty());

        int key = 123;
        REQUIRE(hashTable.getValue(&key) == nullptr);
    }

    SECTION("Stress Test with High Volume Data") {
        hashTable.set(4, 4, 1024, nullptr, 0, false, 0, "testHashTable");

        for (int i = 0; i < 1000000; ++i) {
            REQUIRE(hashTable.addKey(&i, &i));
        }

        for (int i = 0; i < 1000000; ++i) {
            REQUIRE(*(int*)hashTable.getValue(&i) == i);
        }
    };

    SECTION("Test getNumDups") {
        hashTable.set(4, 4, 10, nullptr, 0, true, 0, "testHashTable");

        int key = 123;
        int value1 = 456;
        int value2 = 789;
        int value3 = 321;

        REQUIRE(hashTable.addKey(&key, &value1));
        REQUIRE(hashTable.addKey(&key, &value2));
        REQUIRE(hashTable.getNumDups() == 1);
        REQUIRE(hashTable.addKey(&key, &value3));
        REQUIRE(hashTable.getNumDups() == 2);
    }

    SECTION("Test isEmpty") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key = 123;
        int value = 456;

        REQUIRE(hashTable.isTableEmpty());
        REQUIRE(hashTable.isEmpty(&key));

        REQUIRE(hashTable.addKey(&key, &value));
        REQUIRE_FALSE(hashTable.isEmpty(&key));
    }

    SECTION("Test isTableEmpty") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        REQUIRE(hashTable.isTableEmpty());

        int key = 123;
        int value = 456;

        REQUIRE(hashTable.addKey(&key, &value));
        REQUIRE_FALSE(hashTable.isTableEmpty());
    }

    SECTION("Test isInTable") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key = 123;
        int value = 456;

        REQUIRE_FALSE(hashTable.isInTable(&key));

        REQUIRE(hashTable.addKey(&key, &value));
        REQUIRE(hashTable.isInTable(&key));
    }

    SECTION("Test enable/disableWrites") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key = 123;
        int value = 456;

        hashTable.disableWrites();
        REQUIRE_FALSE(hashTable.addKey(&key, &value));

        hashTable.enableWrites();
        REQUIRE(hashTable.addKey(&key, &value));
        REQUIRE(*(int*)hashTable.getValue(&key) == value);
    }

    SECTION("Test serialize/deserialize") {
        hashTable.set(4, 4, 10, nullptr, 0, false, 0, "testHashTable");

        int key1 = 123;
        int value1 = 456;
        int key2 = 789;
        int value2 = 101112;

        REQUIRE(hashTable.addKey(&key1, &value1));
        REQUIRE(hashTable.addKey(&key2, &value2));

        int32_t bufSize = 0;
        char* serializedData = hashTable.serialize(&bufSize);
        REQUIRE(serializedData != nullptr);
        REQUIRE(bufSize > 0);

        HashTableX newHashTable;
        REQUIRE(newHashTable.deserialize(serializedData, bufSize, 0));

        REQUIRE(*(int*)newHashTable.getValue(&key1) == value1);
        REQUIRE(*(int*)newHashTable.getValue(&key2) == value2);
    }
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

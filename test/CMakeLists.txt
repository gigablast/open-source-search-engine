find_package(Catch2 3 REQUIRED)
include_directories( ${CMAKE_SOURCE_DIR}/src )

include(Catch)

add_executable(Abbreviations src/AbbreviationsTest.cpp)
target_link_libraries(Abbreviations PRIVATE Catch2::Catch2 osse)
catch_discover_tests(Abbreviations)

add_executable(test1 src/test1.cpp)
target_link_libraries(test1 PRIVATE Catch2::Catch2 osse)
catch_discover_tests(test1)

add_executable(Linkdb src/Linkdb.cpp)
target_link_libraries(Linkdb PRIVATE Catch2::Catch2 osse)
catch_discover_tests(Linkdb)

add_executable(HashTableX src/HashTableXTest.cpp)
target_link_libraries(HashTableX PRIVATE Catch2::Catch2 osse)
catch_discover_tests(HashTableX)

add_executable(ArchiveParser src/ArchiveParserTest.cpp)
target_link_libraries(ArchiveParser PRIVATE Catch2::Catch2 osse)
catch_discover_tests(ArchiveParser)

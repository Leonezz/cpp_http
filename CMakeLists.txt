cmake_minimum_required(VERSION 3.12)
project(cpp-http VERSION 0.1.0 LANGUAGES CXX)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.89 REQUIRED COMPONENTS filesystem url context)
find_package(OpenSSL REQUIRED)

add_library(cpp-http INTERFACE)

target_include_directories(
    cpp-http
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
)

add_executable(example_basic_client examples/client/basic.cpp)
target_link_libraries(example_basic_client
    PRIVATE
    cpp-http
    Boost::filesystem
    Boost::url
    Boost::context
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_executable(example_https_client examples/client/https.cpp)
target_link_libraries(example_https_client
    PRIVATE
    cpp-http
    Boost::filesystem
    Boost::url
    Boost::context
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_executable(example_chunked_client examples/client/chunked.cpp)
target_link_libraries(example_chunked_client
    PRIVATE
    cpp-http
    Boost::filesystem
    Boost::url
    Boost::context
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_executable(example_sse_client examples/client/sse.cpp)
target_link_libraries(example_sse_client
    PRIVATE
    cpp-http
    Boost::filesystem
    Boost::url
    Boost::context
    OpenSSL::SSL
    OpenSSL::Crypto
)

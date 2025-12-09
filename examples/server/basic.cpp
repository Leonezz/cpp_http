#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "server/server.hpp"
#include <boost/asio/detached.hpp>
#include <iostream>

int main() {
  const auto endpoint = boost::asio::ip::tcp::endpoint(
      boost::asio::ip::make_address("127.0.0.1"), 80800);
  auto server = cpp_http::server::server(endpoint);
  server.get("/hello", [](cpp_http::server::request &&req) {
    return std::make_unique<cpp_http::server::dummy_session>(std::move(req));
  });

  std::cout << "endpoint: " << endpoint << '\n';

  boost::asio::io_context ioc;

  boost::asio::spawn(
      ioc, [&server](boost::asio::yield_context yield) { server.run(yield); },
      boost::asio::detached);
  ioc.run();
  return 0;
}
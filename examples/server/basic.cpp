#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "server/request.hpp"
#include "server/response.hpp"
#include "server/server.hpp"
#include "server/service_builder.hpp"
#include <boost/asio/detached.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <iostream>

int main() {
  const auto endpoint = boost::asio::ip::tcp::endpoint(
      boost::asio::ip::make_address("127.0.0.1"), 80800);
  auto server = cpp_http::server::server(endpoint);
  server.get(
      "/hello",
      std::move(cpp_http::server::service_builder{}).build_function_service(
          [](cpp_http::server::request &&request,
             boost::asio::yield_context yield) {
            return std::move(
                       cpp_http::server::response_builder{}.ok().content_type(
                           "text/plain"))
                .body<boost::beast::http::string_body, std::string>(
                    "Hello, World!");
          }));

  std::cout << "endpoint: " << endpoint << '\n';

  boost::asio::io_context ioc;

  boost::asio::spawn(
      ioc, [&server](boost::asio::yield_context yield) { server.run(yield); },
      boost::asio::detached);
  ioc.run();
  return 0;
}
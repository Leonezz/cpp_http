#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "server/request.hpp"
#include "server/response.hpp"
#include "server/server.hpp"
#include "server/service.hpp"
#include "server/service_builder.hpp"
#include "server/util.hpp"
#include <boost/asio/detached.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <iostream>
#include <memory>

int main() {
  const auto endpoint = boost::asio::ip::tcp::endpoint(
      boost::asio::ip::make_address("127.0.0.1"), 12351);
  auto server = cpp_http::server::server(endpoint);
  server.get("/sse",
             std::move(cpp_http::server::service_builder{})
                 .build_service(
                     std::make_unique<cpp_http::server::dummy_sse_service>()));

  server.get("/simple",
             std::move(cpp_http::server::service_builder{})
                 .build_function_service([](cpp_http::server::request &&req,
                                            boost::asio::yield_context yield) {
                   return std::move(cpp_http::server::response_builder{}
                                        .ok()
                                        .version(req.request_cref().version())
                                        .set(boost::beast::http::field::server,
                                             cpp_http::server::server_agent())
                                        .content_type("text/plain"))
                       .body<boost::beast::http::string_body, std::string>(
                           "this is a simple GET response.");
                 }));

  std::cout << "endpoint: " << endpoint << '\n';

  boost::asio::io_context ioc;

  boost::asio::spawn(
      ioc, [&server](boost::asio::yield_context yield) { server.run(yield); },
      boost::asio::detached);
  ioc.run();
  return 0;
}
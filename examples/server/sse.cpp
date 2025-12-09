#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "server/request.hpp"
#include "server/server.hpp"
#include "server/session.hpp"
#include <boost/asio/detached.hpp>
#include <iostream>
#include <memory>

int main() {
  const auto endpoint = boost::asio::ip::tcp::endpoint(
      boost::asio::ip::make_address("127.0.0.1"), 12351);
  auto server = cpp_http::server::server(endpoint);
  server.get("/sse", [](cpp_http::server::request &&req) {
    return std::make_unique<cpp_http::server::dummy_sse_session>(
        std::move(req));
  });
  server.get("/hello", [](cpp_http::server::request &&req) {
    return std::make_unique<cpp_http::server::dummy_session>(std::move(req));
  });
  server.simple_get("/simple", [](cpp_http::server::request &&req,
                                  boost::asio::yield_context yield) {
    boost::beast::http::response<boost::beast::http::dynamic_body> res{
        boost::beast::http::status::ok, req.request_cref().version()};
    res.set(boost::beast::http::field::server, "cpp-http/server");
    res.set(boost::beast::http::field::content_type, "text/plain");
    boost::beast::ostream(res.body()) << "This is a simple GET response.";
    return boost::outcome_v2::success(std::move(res));
  });

  std::cout << "endpoint: " << endpoint << '\n';

  boost::asio::io_context ioc;

  boost::asio::spawn(
      ioc, [&server](boost::asio::yield_context yield) { server.run(yield); },
      boost::asio::detached);
  ioc.run();
  return 0;
}
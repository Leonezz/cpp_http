#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "client/client.hpp"
#include "client/request_builder.hpp"
#include "client/response.hpp"
#include <boost/asio/buffer.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/channel_error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/system/detail/error_code.hpp>
#include <chrono>
#include <iostream>

int main() {
  const auto url = "http://localhost:8080/chunk";
  auto request = cpp_http::client::request_builder()
                     .method(boost::beast::http::verb::get)
                     .timeout(std::chrono::milliseconds(60000))
                     .base_url(url)
                     .build();
  std::cout << "Request URL: " << request.url.buffer() << '\n';
  std::cout << "req: " << request.request << '\n';
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::resolver resolver(ioc);
  boost::asio::experimental::channel<void(boost::system::error_code,
                                          cpp_http::http_chunk)>
      ch(ioc, 10);

  boost::asio::spawn(
      ioc,
      [&](boost::asio::yield_context yield) {
        auto response_outcome =
            cpp_http::client::send<boost::beast::http::string_body,
                                   boost::beast::http::string_body>(request,
                                                                    yield);
        if (!response_outcome) {
          std::cout << " Error: " << response_outcome.error().message() << "\n";
          return;
        }
        std::cout << "Response received for request"
                  << ", done: " << response_outcome.value()->complete() << "\n";
        auto &response = response_outcome.value();
        auto ec = response->read_chunked_encoding(ch, yield);
        if (ec.has_error()) {
          std::cout << "produce error: " << ec.error().category().name() << ":"
                    << ec.error().message() << "\n";
        }
      },
      [&ch](auto &) {
        std::cout << "produce complete" << '\n';
        ch.close();
      });

  boost::asio::spawn(
      ioc,
      [&](boost::asio::yield_context yield) {
        while (true) {
          boost::system::error_code ec;
          auto message = ch.async_receive(yield[ec]);
          if (ec == boost::asio::experimental::error::channel_closed) {
            std::cout << "consume done, channel closed: " << ec.message() << "\n";
            break;
          }
          if (ec) {
            std::cout << "consume error: " << ec.message() << "\n";
          }
          std::cout << "got chunk: " << message.chunked_body << "\n"
                    << "extension: " << message.extensions.value_or("") << "\n";
        }
      },
      [](auto &token) { std::cout << "consume complete\n"; });
  ioc.run();
  return 0;
}
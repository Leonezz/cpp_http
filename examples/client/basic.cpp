#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "client/client.hpp"
#include "client/request_builder.hpp"
#include <boost/asio/detached.hpp>
#include <chrono>
#include <iostream>

int main() {
  const auto *const url = "http://www.baidu.com/";
  auto request = cpp_http::client::request_builder()
                     .method(boost::beast::http::verb::get)
                     .timeout(std::chrono::milliseconds(60000))
                     .base_url(url)
                     .build();
  std::cout << "Request URL: " << request.url.buffer() << '\n';
  std::cout << "req: " << request.request << '\n';
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::resolver resolver(ioc);
  for (int i = 0; i < 200; ++i) {
    boost::asio::spawn(
        ioc,
        [&, i = i](boost::asio::yield_context yield) {
          auto response_outcome =
              cpp_http::client::send<boost::beast::http::string_body,
                                     boost::beast::http::string_body>(request,
                                                                      yield);
          if (!response_outcome) {
            std::cout << i << " Error: " << response_outcome.error().message()
                      << "\n";
            return;
          }
          std::cout << "Response received for request: " << i
                    << ", done: " << response_outcome.assume_value()->complete()
                    << "\n";
          auto &response = response_outcome.value();
          while (!response->complete()) {
            auto chunk_outcome = response->read(yield);
            if (!chunk_outcome) {
              std::cout << "Error reading chunk: "
                        << chunk_outcome.error().message() << "\n";
              return;
            }
            auto chunk = chunk_outcome.value();
            std::cout << chunk.size() << ": " << chunk << std::endl;
          }
        },
        boost::asio::detached);
  }
  ioc.run();
  return 0;
}
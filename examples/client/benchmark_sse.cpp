#include "boost/asio/io_context.hpp"
#include "boost/asio/spawn.hpp"
#include "client/client.hpp"
#include "client/request_builder.hpp"
#include "client/response.hpp"
#include <boost/asio/buffer.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/system/detail/error_code.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char *argv[]) {
  const auto *url = argv[1];
  const auto count = std::stoi(argv[2]);
  const auto interval_ms = std::stoi(argv[3]);
  auto request = cpp_http::client::request_builder()
                     .method(boost::beast::http::verb::get)
                     .timeout(std::chrono::milliseconds(60000))
                     .base_url(url)
                     .build();
  std::cout << "Request URL: " << request.url.buffer() << '\n';
  std::cout << "req: " << request.request << '\n';
  boost::asio::io_context ioc;
  for (int i = 0; i < count; ++i) {
    auto ch = std::make_shared<boost::asio::experimental::channel<void(
        boost::system::error_code, cpp_http::server_sent_event)>>(ioc, 10);
    boost::asio::spawn(
        ioc,
        [ch = ch, &request, i](boost::asio::yield_context yield) {
          auto response_outcome =
              cpp_http::client::send<boost::beast::http::string_body,
                                     boost::beast::http::string_body>(request,
                                                                      yield);
          if (!response_outcome) {
            std::cout << " Error: " << response_outcome.error().message()
                      << "\n";
            return;
          }
          std::cout << i << ": Response received for request"
                    << ", done: " << response_outcome.value()->complete()
                    << "\n";
          auto &response = response_outcome.value();
          auto ec = response->read_sse(*ch, yield);
          if (ec.has_error()) {
            std::cout << "produce error: " << ec.error().category().name()
                      << ":" << ec.error().message() << "\n";
          }
        },
        [ch = ch](auto &) {
          ch->cancel();
          ch->close();
          std::cout << "complete" << '\n';
        });

    boost::asio::spawn(
        ioc,
        [ch = ch, i, interval_ms](boost::asio::yield_context yield) {
          uint64_t prev_recv_time = 0;
          while (ch->is_open()) {
            boost::system::error_code ec;
            auto message = ch->async_receive(yield[ec]);
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
            if (prev_recv_time > 0) {
              if (now - prev_recv_time > interval_ms &&
                  now - prev_recv_time - interval_ms > 10) {
                std::cout << "recv timeout, now: " << now
                          << ", prev: " << prev_recv_time
                          << ", diff: " << (now - prev_recv_time) << "\n";
              }
            }
            prev_recv_time = now;
            if (ec) {
              std::cout << "error consume: " << ec.to_string() << "\n";
              break;
            }
            std::cout << i << ": got event: " << message.event.value_or("")
                      << "\n"
                      << "id: " << message.id.value_or("") << "\n"
                      << "data: " << message.data.value_or("") << "\n";
          }
        },
        [](auto &token) { std::cout << "consume complete\n"; });
  }
  ioc.run();
  return 0;
}
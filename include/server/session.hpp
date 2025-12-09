#pragma once
#include "message.hpp"
#include "server/request.hpp"
#include "server/util.hpp"
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/string_type.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <boost/json.hpp>
#include <boost/json/parse.hpp>
#include <boost/outcome.hpp>
#include <boost/variant2/variant.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <variant>

namespace cpp_http::server {

class session {
protected:
  request req_;

public:
  explicit session(request &&req) : req_(std::move(req)) {}
  virtual ~session() = default;
  session(session &) = delete;
  session &operator=(session &) = delete;
  session(session &&) noexcept = default;
  session &operator=(session &&) noexcept = default;
  virtual void handle_request(
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              boost::beast::flat_buffer>)> &tx,
      boost::asio::yield_context yield) = 0;

  virtual void setup_response(
      const request &req,
      boost::beast::http::response<boost::beast::http::dynamic_body> &res,
      boost::asio::yield_context yield) {
    res.set(boost::beast::http::field::server, server_agent());
  }
};

class chunked_sesion : public session {
  virtual void do_handle_chunked_request(
      request req,
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              boost::beast::flat_buffer>)> &tx,
      boost::asio::yield_context yield) = 0;

public:
  explicit chunked_sesion(request &&body) : session(std::move(body)) {}
  virtual ~chunked_sesion() = default;
  chunked_sesion(chunked_sesion &&) noexcept = default;
  chunked_sesion &operator=(chunked_sesion &&) noexcept = default;
  chunked_sesion(chunked_sesion &) = delete;
  chunked_sesion &operator=(chunked_sesion &) = delete;
  void handle_request(
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              boost::beast::flat_buffer>)> &tx,
      boost::asio::yield_context yield) override {

    boost::beast::http::response<boost::beast::http::dynamic_body> res{
        boost::beast::http::status::ok, req_.request_cref().version()};
    setup_response(req_, res, yield);
    tx.async_send(boost::beast::error_code{}, std::move(res), yield);
    do_handle_chunked_request(req_, tx, yield);
  }

  void setup_response(
      const request &req,
      boost::beast::http::response<boost::beast::http::dynamic_body> &res,
      boost::asio::yield_context yield) override {
    session::setup_response(req, res, yield);
    res.set(boost::beast::http::field::transfer_encoding, "chunked");
    res.set(boost::beast::http::field::cache_control, "no-cache");
    res.chunked(true);
  }
};

class sse_session : public chunked_sesion {
  virtual void do_handle_sse_request(
      request req,
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              ::cpp_http::server_sent_event>)> &tx,
      boost::asio::yield_context yield) = 0;

  void do_handle_chunked_request(
      request req,
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              boost::beast::flat_buffer>)> &tx,
      boost::asio::yield_context yield) override {
    auto sse_ch = std::make_shared<boost::asio::experimental::channel<void(
        boost::beast::error_code,
        std::variant<
            boost::beast::http::response<boost::beast::http::dynamic_body>,
            ::cpp_http::server_sent_event>)>>(yield.get_executor(), 10);
    boost::asio::spawn(
        yield.get_executor(),
        [this, req = std::move(req),
         sse_tx = sse_ch](boost::asio::yield_context yield) mutable {
          do_handle_sse_request(std::move(req), *sse_tx, yield);
        },
        [sse_tx = sse_ch](auto &) {
          sse_tx->cancel();
          sse_tx->close();
        });
    boost::beast::error_code ec;
    while (sse_ch->is_open()) {
      auto message = sse_ch->async_receive(yield[ec]);
      if (ec) {
        break;
      }
      std::visit(
          overload{
              [&tx, yield, &ec](
                  boost::beast::http::response<boost::beast::http::dynamic_body>
                      msg) {
                tx.async_send(boost::beast::error_code{}, std::move(msg),
                              yield[ec]);
              },
              [&tx, yield, &ec](::cpp_http::server_sent_event sse) {
                if (!sse.valid()) {
                  return;
                }
                boost::beast::flat_buffer sse_buffer;
                if (sse.event.has_value()) {
                  boost::beast::ostream(sse_buffer)
                      << "event: " << sse.event.value() << "\n";
                }
                if (sse.id.has_value()) {
                  boost::beast::ostream(sse_buffer)
                      << "id: " << sse.id.value() << "\n";
                }
                if (sse.data.has_value()) {
                  boost::beast::ostream(sse_buffer)
                      << "data: " << sse.data.value() << "\n";
                }
                if (sse.retry.has_value()) {
                  boost::beast::ostream(sse_buffer)
                      << "retry: " << std::to_string(sse.retry.value()) << "\n";
                }
                boost::beast::ostream(sse_buffer) << "\n";
                tx.async_send(boost::beast::error_code{}, std::move(sse_buffer),
                              yield[ec]);
              }},
          std::move(message));

      if (ec) {
        break;
      }
    }
  }

public:
  explicit sse_session(request &&body) : chunked_sesion(std::move(body)) {}
  sse_session(sse_session &&) noexcept = default;
  sse_session &operator=(sse_session &&) noexcept = default;
  sse_session(sse_session &) = delete;
  sse_session &operator=(sse_session &) = delete;

  virtual ~sse_session() = default;

  void setup_response(
      const request &req,
      boost::beast::http::response<boost::beast::http::dynamic_body> &res,
      boost::asio::yield_context yield) override {
    chunked_sesion::setup_response(req, res, yield);
    res.set(boost::beast::http::field::content_type, "text/event-stream");
  }
};

class dummy_session : public session {
public:
  explicit dummy_session(request &&req) : session(std::move(req)) {}
  dummy_session(dummy_session &&) noexcept = default;
  dummy_session &operator=(dummy_session &&) noexcept = default;
  dummy_session(dummy_session &) = delete;
  dummy_session &operator=(dummy_session &) = delete;

  virtual ~dummy_session() = default;
  void handle_request(
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              boost::beast::flat_buffer>)> &tx,
      boost::asio::yield_context yield) override {
    boost::beast::http::response<boost::beast::http::dynamic_body> res{
        boost::beast::http::status::ok, req_.request_cref().version()};
    res.set(boost::beast::http::field::server, server_agent());
    res.set(boost::beast::http::field::content_type, "text/html");
    res.keep_alive(false);
    boost::beast::ostream(res.body()) << "<h1>Hello World!</h1>";
    tx.async_send(boost::beast::error_code{}, std::move(res), yield);
    tx.close();
  }
};

class dummy_sse_session : public sse_session {
  void do_handle_sse_request(
      request req,
      boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              ::cpp_http::server_sent_event>)> &tx,
      boost::asio::yield_context yield) override {
    ::cpp_http::server_sent_event sse;
    boost::asio::steady_timer timer(yield.get_executor());
    uint64_t id = 0;
    boost::beast::error_code ec;
    while (true) {
      sse.event = "message";
      sse.id = std::to_string(id++);
      sse.data = "hello world";
      tx.async_send(boost::beast::error_code{}, std::move(sse), yield[ec]);
      if (ec) {
        break;
      }
      timer.expires_after(std::chrono::milliseconds{50});
      timer.async_wait(yield);
    }
  }

public:
  explicit dummy_sse_session(request &&body) : sse_session(std::move(body)) {}
  dummy_sse_session(dummy_sse_session &&) noexcept = default;
  dummy_sse_session &operator=(dummy_sse_session &&) noexcept = default;
  dummy_sse_session(dummy_sse_session &) = delete;
  dummy_sse_session &operator=(dummy_sse_session &) = delete;

  virtual ~dummy_sse_session() = default;
};
} // namespace cpp_http::server
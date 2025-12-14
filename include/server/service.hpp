#pragma once
#include "message.hpp"
#include "server/errors.hpp"
#include "server/request.hpp"
#include "server/response.hpp"
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/detect_ssl.hpp>
#include <boost/beast/core/string_type.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/move/detail/placement_new.hpp>
#include <boost/outcome/result.hpp>
#include <boost/outcome/success_failure.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>
#include <boost/smart_ptr/make_local_shared_array.hpp>
#include <boost/system/detail/error_code.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
namespace cpp_http::server {
class service {
public:
  virtual ~service() = default;
  virtual result<response>
  handle_request(request &&request, boost::asio::yield_context yield) = 0;
};

class chunked_service : public service {
public:
  virtual ~chunked_service() override = default;
  virtual result<empty_response>
  handle_chunked_request(request &&request,
                         boost::local_shared_ptr<streaming_channel> tx,
                         boost::asio::yield_context yield) = 0;

  result<response>
  handle_request(request &&request, boost::asio::yield_context yield) override {
    auto channel =
        boost::make_local_shared<streaming_channel>(yield.get_executor(), 10);
    auto header = handle_chunked_request(std::move(request), channel, yield);
    if (header.has_error()) {
      return header.error();
    }
    if (header.value().result() == boost::beast::http::status::ok) {
      header.value().set(boost::beast::http::field::transfer_encoding,
                         "chunked");
      header.value().chunked(true);
    }

    return response{std::move(header).value(), std::move(channel)};
  }
};

class sse_service : public chunked_service {
public:
  virtual ~sse_service() override = default;
  virtual result<empty_response>
  handle_sse_request(request &&request,
                     boost::local_shared_ptr<streaming_channel> tx,
                     boost::asio::yield_context yield) = 0;
  result<empty_response>
  handle_chunked_request(request &&request,
                         boost::local_shared_ptr<streaming_channel> tx,
                         boost::asio::yield_context yield) override {
    auto response =
        handle_sse_request(std::move(request), std::move(tx), yield);
    if (response.has_value() &&
        response.value().base().result() == boost::beast::http::status::ok) {
      response.value().set(boost::beast::http::field::content_type,
                           "text/event-stream");
    }
    return response;
  }
};

class dummy_service : public service {
public:
  ~dummy_service() final = default;
  result<response>
  handle_request(request &&request, boost::asio::yield_context yield) final {
    return std::move(response_builder{}
                         .status(boost::beast::http::status::ok)
                         .version(request.request_cref().version())
                         .content_type("text/plain")
                         .keep_alive(false))
        .body<boost::beast::http::string_body, std::string>("Hello World!");
  }
};

class dummy_sse_service : public sse_service {
public:
  ~dummy_sse_service() final = default;
  result<empty_response>
  handle_sse_request(request &&request,
                     boost::local_shared_ptr<streaming_channel> tx,
                     boost::asio::yield_context yield) final {
    boost::asio::spawn(
        yield.get_executor(),
        [tx](boost::asio::yield_context yield) { event_loop(tx, yield); },
        [tx](auto &) {
          tx->cancel();
          tx->close();
        });
    return std::move(response_builder{}
                         .status(boost::beast::http::status::ok)
                         .version(request.request_cref().version())
                         .keep_alive(true))
        .empty();
  }

  static void event_loop(boost::local_shared_ptr<streaming_channel> tx,
                         boost::asio::yield_context yield) {
    ::cpp_http::server_sent_event sse;
    boost::asio::steady_timer timer(yield.get_executor());
    uint64_t id = 0;
    while (tx->is_open()) {
      sse.event = "message";
      sse.id = std::to_string(id++);
      sse.data = "This is a server-sent event message.";
      tx->async_send({}, std::move(sse).to_http_chunk(), yield);
      timer.expires_after(std::chrono::milliseconds{50});
      timer.async_wait(yield);
    }
  }
};

class function_service : public service {
  using service_func_t = std::function<result<response>(
      request &&request, boost::asio::yield_context yield)>;

  service_func_t func_;

public:
  template <typename F>
  explicit function_service(F func) : func_(std::move(func)) {}
  ~function_service() override = default;
  result<response>
  handle_request(request &&request, boost::asio::yield_context yield) override {
    return func_(std::move(request), yield);
  }
};

class pre_request_service : public service {
  using pre_request_handler_t =
      std::function<request(request &&, boost::asio::yield_context)>;
  pre_request_handler_t handler_;
  std::unique_ptr<service> inner_;

public:
  template <typename F>
  explicit pre_request_service(F handler, std::unique_ptr<service> inner)
      : handler_(std::move(handler)), inner_(std::move(inner)) {}
  ~pre_request_service() override = default;
  result<response>
  handle_request(request &&request, boost::asio::yield_context yield) override {
    auto new_request = handler_(std::move(request), yield);
    return inner_->handle_request(std::move(new_request), yield);
  }
};

class after_response_service : public service {
  using after_response_handler_t = std::function<result<response>(
      result<response> &&, boost::asio::yield_context)>;
  after_response_handler_t handler_;
  std::unique_ptr<service> inner_;

public:
  template <typename F>
  explicit after_response_service(F handler, std::unique_ptr<service> inner)
      : handler_(std::move(handler)), inner_(std::move(inner)) {}
  ~after_response_service() override = default;
  result<response>
  handle_request(request &&request, boost::asio::yield_context yield) override {
    auto response = inner_->handle_request(std::move(request), yield);
    return handler_(std::move(response), yield);
  }
};
} // namespace cpp_http::server
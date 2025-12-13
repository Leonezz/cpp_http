#pragma once
#include "message.hpp"
#include "server/util.hpp"
#include <algorithm>
#include <boost/asio/buffer.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/file_body_fwd.hpp>
#include <boost/beast/http/impl/message_generator.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/system/detail/error_code.hpp>
#include <string_view>
#include <utility>
#include <variant>

namespace cpp_http::server {
struct abstract_response {
  virtual ~abstract_response() = default;
  virtual boost::beast::http::response_header<> &header_ref() = 0;
  virtual const boost::beast::http::response_header<> &header_cref() const = 0;
  virtual boost::beast::http::message_generator to_generator() && = 0;
};

template <class Body> class response_impl : abstract_response {
  boost::beast::http::response<Body> msg_;

public:
  explicit response_impl(boost::beast::http::response<Body> &&msg)
      : msg_(std::move(msg)) {}

  boost::beast::http::response_header<> &header_ref() override {
    return msg_.base();
  }
  const boost::beast::http::response_header<> &header_cref() const override {
    return msg_.base();
  };

  boost::beast::http::message_generator to_generator() && override {
    msg_.prepare_payload();
    return std::move(msg_);
  }
};

class mutable_response {
  std::unique_ptr<abstract_response> impl_;

public:
  template <class Body>
  explicit mutable_response(boost::beast::http::response<Body> msg)
      : impl_(std::make_unique<response_impl<Body>>(std::move(msg))) {}

  void set(boost::beast::http::field f, boost::beast::string_view value) {
    impl_->header_ref().set(f, value);
  }

  boost::beast::http::response_header<> &header_ref() {
    return impl_->header_ref();
  }

  const boost::beast::http::response_header<> &header_cref() const {
    return impl_->header_cref();
  };

  boost::beast::http::message_generator to_generator() && {
    return std::move(*impl_).to_generator();
  }
};

using streaming_channel = boost::asio::experimental::channel<void(
    boost::system::error_code, http_chunk)>;
using empty_response =
    boost::beast::http::response<boost::beast::http::empty_body>;
class outgoing_response {
public:
  struct streaming_response {
    empty_response header_;
    boost::local_shared_ptr<streaming_channel> rx_;
    explicit streaming_response(empty_response header,
                                boost::local_shared_ptr<streaming_channel> rx)
        : header_(std::move(header)), rx_(std::move(rx)) {}
    streaming_response(const streaming_response &) = delete;
    streaming_response &operator=(const streaming_response &) = delete;
    streaming_response(streaming_response &&) noexcept = default;
    streaming_response &operator=(streaming_response &&) noexcept = default;
    ~streaming_response() = default;
    boost::beast::http::response_header<> &header_ref() {
      return header_.base();
    }
    const boost::beast::http::response_header<> &header_cref() const {
      return header_.base();
    };
  };

private:
  std::variant<mutable_response, streaming_response> response_;

public:
  explicit outgoing_response(mutable_response &&res)
      : response_(std::move(res)) {}
  explicit outgoing_response(streaming_response &&res)
      : response_(std::move(res)) {}
  template <typename Response>
  explicit outgoing_response(Response res)
      : outgoing_response(mutable_response{std::move(res)}) {}
  explicit outgoing_response(empty_response header,
                             boost::local_shared_ptr<streaming_channel> rx)
      : outgoing_response(
            streaming_response(std::move(header), std::move(rx))) {}
  ~outgoing_response() = default;
  outgoing_response(const outgoing_response &) = delete;
  outgoing_response &operator=(const outgoing_response &) = delete;
  outgoing_response(outgoing_response &&) noexcept = default;
  outgoing_response &operator=(outgoing_response &&) noexcept = default;

  const boost::beast::http::response_header<> &header_cref() const {
    return std::visit(
        overload{[](const mutable_response &res)
                     -> boost::beast::http::response_header<> const & {
                   return res.header_cref();
                 },
                 [](const streaming_response &res)
                     -> boost::beast::http::response_header<> const & {
                   return res.header_cref();
                 }},
        response_);
  }

  boost::beast::http::response_header<> &header_ref() {
    return std::visit(overload{[](mutable_response &res)
                                   -> boost::beast::http::response_header<> & {
                                 return res.header_ref();
                               },
                               [](streaming_response &res)
                                   -> boost::beast::http::response_header<> & {
                                 return res.header_ref();
                               }},
                      response_);
  }

  template <typename F> void visit(F &&f) const {
    std::visit(std::forward<F>(f), response_);
  }

  template <typename F> void visit(F &&f) {
    std::visit(std::forward<F>(f), response_);
  }

  void async_write(boost::beast::tcp_stream &stream,
                   boost::asio::yield_context yield) && {
    const auto async_write_basic_response =
        [&stream, yield](mutable_response &&response) {
          boost::beast::async_write(stream, std::move(response).to_generator(),
                                    yield);
        };
    const auto async_write_streaming_response = [&stream,
                                                 yield](streaming_response
                                                            response) {
      boost::beast::http::response_serializer<boost::beast::http::empty_body>
          serializer(response.header_);
      boost::beast::http::async_write_header(stream, serializer, yield);
      http_chunk chunk{};
      while (response.rx_->is_open()) {
        chunk = response.rx_->async_receive(yield);
        if (!chunk.valid()) {
          continue;
        }
        boost::asio::async_write(stream, chunk.to_chunk_body(), yield);
      }
      boost::asio::async_write(stream, boost::beast::http::make_chunk_last(),
                               yield);
    };
    std::visit(
        overload{
            async_write_basic_response,
            async_write_streaming_response,
        },
        std::move(response_));
  }
};

class response_builder {
  empty_response header_;

public:
  response_builder() = default;
  response_builder(const response_builder &) = default;
  response_builder &operator=(const response_builder &) = default;
  response_builder(response_builder &&) noexcept = default;
  response_builder &operator=(response_builder &&) noexcept = default;
  ~response_builder() = default;

  response_builder &status(boost::beast::http::status status) {
    header_.result(status);
    return *this;
  }

  response_builder &ok() { return status(boost::beast::http::status::ok); }
  response_builder &bad_request() {
    return status(boost::beast::http::status::bad_request);
  }
  response_builder &server_error() {
    return status(boost::beast::http::status::internal_server_error);
  }

  response_builder &version(unsigned version) {
    header_.version(version);
    return *this;
  }

  response_builder &keep_alive(bool keep_alive) {
    header_.keep_alive(keep_alive);
    return *this;
  }

  response_builder &content_type(std::string_view content_type) {
    header_.set(boost::beast::http::field::content_type, content_type);
    return *this;
  }

  response_builder &reason(std::string_view reason) {
    header_.reason(reason);
    return *this;
  }

  response_builder &set(boost::beast::http::field field,
                        std::string_view value) {
    header_.set(field, value);
    return *this;
  }

  response_builder &set(std::string_view field, std::string_view value) {
    header_.set(field, value);
    return *this;
  }

  template <typename Body, typename Param = Body>
  outgoing_response body(Param &&body) && {
    boost::beast::http::response<Body> res{std::move(header_).base()};
    res.body() = std::forward<Param>(body);
    return outgoing_response(std::move(res));
  }

  empty_response empty() && { return std::move(header_); }

  outgoing_response
  streaming(boost::local_shared_ptr<streaming_channel> rx) && {
    header_.chunked(true);
    return outgoing_response(std::move(header_), std::move(rx));
  }
};
} // namespace cpp_http::server
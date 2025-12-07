#pragma once
#include "boost/asio/experimental/channel.hpp"
#include "boost/asio/read_until.hpp"
#include "boost/asio/spawn.hpp"
#include "boost/asio/streambuf.hpp"
#include "boost/beast/core/flat_buffer.hpp"
#include "boost/beast/core/tcp_stream.hpp"
#include "boost/beast/http/dynamic_body_fwd.hpp"
#include "boost/beast/http/impl/read.hpp"
#include "boost/outcome.hpp"
#include <boost/asio/basic_streambuf_fwd.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core/basic_stream.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/impl/error.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/string_type.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/parser_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/core/detail/string_view.hpp>
#include <boost/outcome/result.hpp>
#include <boost/outcome/success_failure.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/url/url.hpp>
#include <boost/utility/string_view_fwd.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace cpp_http::client {
struct chunk {
  std::string chunked_body;
  std::optional<std::string> extensions;
};

struct server_sent_event {
  std::optional<std::string> event;
  std::optional<std::string> id;
  std::optional<std::string> data;
  std::optional<uint64_t> retry;

  inline bool valid() const {
    return event.has_value() || id.has_value() || data.has_value() ||
           retry.has_value();
  }
};
template <class Body = boost::beast::http::dynamic_body> class http_response {
public:
  using value_type = typename Body::value_type;
  explicit http_response(std::unique_ptr<boost::beast::tcp_stream> stream)
      : stream_(std::move(stream)), done_(false) {}
  explicit http_response(
      std::unique_ptr<boost::asio::ssl::stream<boost::beast::tcp_stream>>
          ssl_stream)
      : ssl_stream_(std::move(ssl_stream)), done_(false) {}
  ~http_response() { close(); }

  inline boost::outcome_v2::result<void>
  init_parser(boost::asio::yield_context yield) {
    if (stream_) {
      boost::beast::http::async_read_header(*stream_, buffer_, parser_,
                                            yield[ec_]);
    } else if (ssl_stream_) {
      boost::beast::http::async_read_header(*ssl_stream_, buffer_, parser_,
                                            yield[ec_]);
    }
    if (ec_) {
      return ec_;
    }

    // detect content type / transfer encoding
    auto ctype = parser_.get().base()["Content-Type"];
    auto te = parser_.get().base()["Transfer-Encoding"];
    sse_ = ctype.find("text/event-stream") != std::string::npos;
    chunked_ = te.find("chunked") != std::string::npos;
    if (stream_) {
      stream_->expires_never();
    } else if (ssl_stream_) {
      boost::beast::get_lowest_layer(*ssl_stream_).expires_never();
    }
  }

  inline auto status() const { return parser_.get().result(); }

  inline auto reason() const { return parser_.get().reason(); }

  inline auto is_redirection() const {
    auto status_code = status();
    return status_code == boost::beast::http::status::moved_permanently ||
           status_code == boost::beast::http::status::found ||
           status_code == boost::beast::http::status::see_other ||
           status_code == boost::beast::http::status::temporary_redirect ||
           status_code == boost::beast::http::status::permanent_redirect;
  }

  inline const auto &headers() const { return parser_.get().base(); }

  inline std::optional<boost::urls::url> redirect_url() const {
    if (!is_redirection()) {
      return std::nullopt;
    }
    auto loc = parser_.get().base()["Location"];
    if (loc.empty()) {
      return std::nullopt;
    }
    try {
      boost::urls::url url(loc);
      return url;
    } catch (...) {
      return std::nullopt;
    }
  }

  inline auto is_ok() const {
    return status() == boost::beast::http::status::ok;
  }

  inline boost::outcome_v2::result<void>
  read_sse(boost::asio::experimental::channel<void(boost::system::error_code,
                                                   server_sent_event)> &tx,
           boost::asio::yield_context yield) {
    if (done_) {
      return boost::asio::error::eof;
    }
    if (!sse_) {
      return boost::beast::http::error::bad_transfer_encoding;
    }
    if (chunked_) {
      return read_chunked_sse(tx, yield);
    }
    boost::asio::streambuf buffer{};
    while (!done_) {
      size_t line_length{};
      if (stream_) {
        line_length =
            boost::asio::async_read_until(*stream_, buffer, "\n\n", yield[ec_]);
      } else if (ssl_stream_) {
        line_length = boost::asio::async_read_until(*ssl_stream_, buffer,
                                                    "\n\n", yield[ec_]);
      }
      if (ec_) {
        if (ec_ == boost::beast::http::error::need_buffer) {
          continue;
        }
        if (ec_ == boost::asio::error::eof) {
          done_ = true;
          break;
        } else {
          return ec_;
        }
      }

      std::string block{static_cast<const char *>(buffer.data().data()),
                        line_length};
      buffer.consume(line_length);

      auto message = parse_sse_block(std::move(block));
      if (!message.has_value()) {
        continue;
      }
      tx.async_send(ec_, std::move(message.value()), yield[ec_]);
      if (ec_) {
        return ec_;
      }
    }
    return ec_;
  }

  inline boost::outcome_v2::result<void> read_chunked_encoding(
      boost::asio::experimental::channel<void(boost::system::error_code, chunk)>
          &tx,
      boost::asio::yield_context yield) {
    if (done_) {
      return boost::asio::error::eof;
    }
    if (!chunked_) {
      return boost::beast::http::error::bad_transfer_encoding;
    }
    parser_.body_limit(std::numeric_limits<std::uint64_t>::max());
    chunk message;
    auto on_chunk_header = [&message](uint64_t size,
                                         boost::core::string_view extensions,
                                         boost::beast::error_code &ec) {
      message.extensions = extensions;
      message.chunked_body.reserve(size);
    };
    auto on_chunk_body = [&message, &tx, yield](
                             uint64_t remain, boost::core::string_view body,
                             boost::beast::error_code &ec) {
      message.chunked_body.append(body);
      if (remain == body.size()) {
        // tx.async_send(ec, std::move(message), yield[ec]);
        tx.try_send(ec, std::move(message));
      }
      return body.length();
    };
    return read_chunked_encoding(on_chunk_header, on_chunk_body, yield);
  }

  // read next available chunk
  inline boost::outcome_v2::result<value_type>
  read(boost::asio::yield_context yield) {
    if (done_) {
      return boost::asio::error::eof;
    }
    if (chunked_ || sse_) {
      return boost::beast::http::error::bad_transfer_encoding;
    }
    parser_.get().body().clear();
    if (stream_) {
      boost::beast::http::async_read(*stream_, buffer_, parser_, yield[ec_]);
    } else if (ssl_stream_) {
      boost::beast::http::async_read(*ssl_stream_, buffer_, parser_,
                                     yield[ec_]);
    }
    if (ec_) {
      if (ec_ == boost::beast::http::error::need_buffer)
        return read(yield);
      if (ec_ == boost::asio::error::eof)
        done_ = true;
      else {
        return boost::outcome_v2::failure(ec_);
      }
    }

    auto &body_data = parser_.get().body();
    done_ = parser_.is_done();
    return body_data;
  }

  inline bool complete() const { return done_; }

  inline void close() {
    if (stream_) {
      stream_->socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                                 ec_);
      // not_connected happens sometimes
      // so don't bother reporting it.
      if (ec_ && ec_ != boost::beast::errc::not_connected) {
      }
    }
    if (ssl_stream_) {
      // do nothing is ok, ssl shutdown will require a yield_context
    }
    done_ = true;
  }

  inline boost::beast::http::response_header<Body> header() const {
    return parser_.get();
  }

private:
  inline static void parse_sse_line(std::string line,
                                    server_sent_event &event) {
    auto colon = line.find(':');
    std::string field;
    std::string value;
    if (colon == std::string::npos) {
      field = std::move(line);
    } else {
      field = line.substr(0, colon);
      value = line.substr(colon + 1);
      if (boost::string_view{value}.starts_with(' ')) {
        value.erase(0, 1);
      }
    }
    if (field == "data") {
      if (!event.data.has_value()) {
        event.data = std::move(value);
      } else {
        event.data->append("\n");
        event.data->append(std::move(value));
      }
    } else if (field == "event") {
      event.event = std::move(value);
    } else if (field == "id") {
      event.id = std::move(value);
    } else if (field == "retry") {
      uint64_t v{};
      if (auto err = std::from_chars(value.c_str(),
                                     value.c_str() + value.size(), v, 10);
          err.ec == std::error_code{}) {
        event.retry = v;
      }
    }
  }

  inline static std::optional<server_sent_event>
  parse_sse_block(std::string block) {
    if (block.empty()) {
      return {};
    }
    server_sent_event message;
    std::istringstream iss{block};
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\n') {
        line.pop_back();
      }
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      parse_sse_line(std::move(line), message);
    }
    if (message.valid()) {
      return message;
    }
    return {};
  }

  template <class OnChunkHeader, class OnChunkbody>
  inline boost::outcome_v2::result<void>
  read_chunked_encoding(OnChunkHeader &on_chunk_header,
                        OnChunkbody &on_chunk_body,
                        boost::asio::yield_context yield) {
    if (done_) {
      return boost::asio::error::eof;
    }
    if (!chunked_) {
      return boost::beast::http::error::bad_transfer_encoding;
    }
    parser_.body_limit(std::numeric_limits<std::uint64_t>::max());
    parser_.on_chunk_header(on_chunk_header);
    parser_.on_chunk_body(on_chunk_body);

    while (!done_) {
      if (stream_) {
        boost::beast::http::async_read(*stream_, buffer_, parser_, yield[ec_]);
      } else if (ssl_stream_) {
        boost::beast::http::async_read(*ssl_stream_, buffer_, parser_,
                                       yield[ec_]);
      }
      done_ = true;
      if (ec_) {
        if (ec_ == boost::beast::http::error::need_buffer) {
          continue;
        }
        if (ec_ == boost::asio::error::eof) {
          return boost::outcome_v2::success();
        } else {
          return boost::outcome_v2::failure(ec_);
        }
      }
    }
    if (ec_) {
      return ec_;
    }
    return boost::outcome_v2::success();
  }

  inline boost::outcome_v2::result<void> read_chunked_sse(
      boost::asio::experimental::channel<void(boost::system::error_code,
                                              server_sent_event)> &tx,
      boost::asio::yield_context yield) {
    if (done_) {
      return boost::asio::error::eof;
    }
    if (!sse_ || !chunked_) {
      return boost::beast::http::error::bad_transfer_encoding;
    }
    std::string block;
    parser_.body_limit(std::numeric_limits<std::uint64_t>::max());
    parser_.get().keep_alive(true);
    parser_.eager(true);
    auto on_chunk_header = [&block](uint64_t size,
                                         boost::core::string_view extensions,
                                         boost::beast::error_code &ec) {
      block.reserve(size);
    };
    auto on_chunk_body = [&tx, yield, &block,
                          this](uint64_t remain, boost::core::string_view body,
                                boost::beast::error_code &ec) {
      block.append(body);
      if (body.size() == remain) {
        if (!boost::core::string_view{block}.ends_with("\n\n")) {
          return body.length();
        }
        auto message = parse_sse_block(std::move(block));
        if (!message.has_value()) {
          return body.length();
        }
        // tx.async_send(ec, std::move(message), yield[ec]);
        tx.try_send(ec, std::move(message.value()));
      }
      return body.length();
    };
    return read_chunked_encoding(on_chunk_header, on_chunk_body, yield);
  }

private:
  std::unique_ptr<boost::beast::tcp_stream> stream_;
  std::unique_ptr<boost::asio::ssl::stream<boost::beast::tcp_stream>>
      ssl_stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::response_parser<Body> parser_;
  boost::beast::error_code ec_;
  bool done_;
  bool sse_ = false;
  bool chunked_ = false;
};
} // namespace cpp_http::client

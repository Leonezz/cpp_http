#pragma once
#include <boost/asio/buffer.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <optional>
#include <string>
namespace cpp_http {
struct http_chunk {
  std::string chunked_body;
  std::optional<std::string> extensions;

  http_chunk() = default;
  explicit http_chunk(std::string body, std::optional<std::string> ext)
      : chunked_body(std::move(body)), extensions(std::move(ext)) {}
  explicit http_chunk(std::string body) : http_chunk(std::move(body), {}) {}
  ~http_chunk() = default;
  http_chunk(const http_chunk &) = default;
  http_chunk &operator=(const http_chunk &) = default;
  http_chunk(http_chunk &&) noexcept = default;
  http_chunk &operator=(http_chunk &&) noexcept = default;

  [[nodiscard]] inline bool valid() const { return !chunked_body.empty(); }

  boost::beast::http::chunk_body<boost::asio::const_buffer>
  to_chunk_body() const {
    auto buffer = boost::asio::buffer(chunked_body);
    if (extensions.has_value()) {
      return boost::beast::http::make_chunk(buffer, extensions.value());
    }
    return boost::beast::http::make_chunk(buffer);
  }
};

struct server_sent_event {
  std::optional<std::string> event;
  std::optional<std::string> id;
  std::optional<std::string> data;
  std::optional<uint64_t> retry;

  [[nodiscard]] inline bool valid() const {
    return event.has_value() || id.has_value() || data.has_value() ||
           retry.has_value();
  }

  [[nodiscard]] inline http_chunk to_http_chunk() && {
    std::string sse_body;
    if (event.has_value()) {
      sse_body.append("event: ").append(std::move(event).value()).append("\n");
    }
    if (id.has_value()) {
      sse_body.append("id: ").append(std::move(id).value()).append("\n");
    }
    if (data.has_value()) {
      sse_body.append("data: ").append(std::move(data).value()).append("\n");
    }
    if (retry.has_value()) {
      sse_body.append("retry: ")
          .append(std::to_string(retry.value()))
          .append("\n");
    }
    return http_chunk{std::move(sse_body), {}};
  }
  [[nodiscard]] inline http_chunk to_http_chunk() const & {
    auto self = *this;
    return std::move(self).to_http_chunk();
  }
};
} // namespace cpp_http
#pragma once
#include "boost/beast/http/message_fwd.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include "boost/beast/http/verb.hpp"
#include "boost/url/url.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/url.hpp>
#include <chrono>
#include <cstdint>

namespace cpp_http::client {
template <class Body = boost::beast::http::string_body> struct http_request {
  boost::urls::url url;
  boost::beast::http::request<Body> request;
  bool auto_redirect{true};
  uint64_t max_redirects{5};
  uint64_t timeout_ms{5000};
};

template <class Body = boost::beast::http::string_body> class request_builder {
public:
  request_builder() : request_{} {}

  // Set HTTP method
  request_builder &method(boost::beast::http::verb m) {
    request_.request.method(m);
    return *this;
  }

  request_builder &base_url(std::string_view base) {
    request_.url = boost::urls::url(base);
    return *this;
  }

  request_builder &param(std::string_view key, std::string_view value) {
    request_.url.params().append({key, value});
    return *this;
  }

  // Set target path
  request_builder &target(std::string_view t) {
    request_.url.set_path(t);
    return *this;
  }

  // Set HTTP version (default 1.1)
  request_builder &version(int v) {
    request_.request.version(v);
    return *this;
  }

  // Add a header
  request_builder &header(std::string_view field, std::string_view value) {
    request_.request.set(field, value);
    return *this;
  }

  // Set body (type depends on Body)
  template <typename T> request_builder &body(T &&b) {
    request_.request.body() = std::forward<T>(b);
    return *this;
  }

  request_builder &timeout(std::chrono::milliseconds duration) {
    request_.timeout_ms = duration.count();
    return *this;
  }

  request_builder &auto_redirect(bool enable) {
    request_.auto_redirect = enable;
    return *this;
  }

  request_builder& max_redirects(uint64_t max_redirects) {
    request_.max_redirects = max_redirects;
    return *this;
  }

  // Build request
  http_request<Body> build() {
    request_.request.target(request_.url.encoded_target());
    request_.request.prepare_payload(); // sets Content-Length etc. if needed
    return request_;
  }

private:
  http_request<Body> request_;
};

} // namespace cpp_http::client
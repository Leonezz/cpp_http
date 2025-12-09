#pragma once
#include <boost/beast/http.hpp>
#include <boost/url.hpp>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

namespace cpp_http::server {
class request {
  boost::beast::http::request<boost::beast::http::string_body> inner;
  std::string path;
  std::unordered_map<std::string, std::string> path_params;
  std::smatch matches;
  std::unordered_multimap<std::string, std::string> query_params;

public:
  explicit request(
      boost::beast::http::request<boost::beast::http::string_body> &&req)
      : inner(std::move(req)) {
    const auto url_view = boost::urls::url_view{inner.target()};
    path = std::string(url_view.encoded_path());
    for (auto param : url_view.params()) {
      query_params.emplace(param.key, param.value);
    }
  }
  request(const request &) = default;
  request &operator=(const request &) = default;
  request(request &&) noexcept = default;
  request &operator=(request &&) noexcept = default;
  ~request() = default;

  [[nodiscard]] constexpr const auto &request_cref() const { return inner; }
  [[nodiscard]] constexpr const auto &path_cref() const { return path; }
  [[nodiscard]] constexpr const auto &path_params_cref() const {
    return path_params;
  }
  [[nodiscard]] constexpr auto &path_params_ref() { return path_params; }
  [[nodiscard]] constexpr const auto &matches_cref() const { return matches; }
  [[nodiscard]] constexpr auto &matches_ref() { return matches; }
  [[nodiscard]] constexpr const auto &query_params_cref() const {
    return query_params;
  }
};
} // namespace cpp_http::server
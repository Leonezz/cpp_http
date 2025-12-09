#pragma once
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/string_type.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
namespace cpp_http::server {
inline constexpr std::string_view server_agent() { return "cpp-http/server"; }
// Return a reasonable mime type based on the extension of a file.
constexpr inline boost::beast::string_view
mime_type(boost::beast::string_view path) {
  using boost::beast::iequals;
  auto const ext = [&path] {
    auto const pos = path.rfind(".");
    if (pos == boost::beast::string_view::npos) {
      return boost::beast::string_view{};
    }
    return path.substr(pos);
  }();
  if (iequals(ext, ".htm")) {
    return "text/html";
  }
  if (iequals(ext, ".html")) {
    return "text/html";
  }
  if (iequals(ext, ".php")) {
    return "text/html";
  }
  if (iequals(ext, ".css")) {
    return "text/css";
  }
  if (iequals(ext, ".txt")) {
    return "text/plain";
  }
  if (iequals(ext, ".js")) {
    return "application/javascript";
  }
  if (iequals(ext, ".json")) {
    return "application/json";
  }
  if (iequals(ext, ".xml")) {
    return "application/xml";
  }
  if (iequals(ext, ".swf")) {
    return "application/x-shockwave-flash";
  }
  if (iequals(ext, ".flv")) {
    return "video/x-flv";
  }
  if (iequals(ext, ".png")) {
    return "image/png";
  }
  if (iequals(ext, ".jpe")) {
    return "image/jpeg";
  }
  if (iequals(ext, ".jpeg")) {
    return "image/jpeg";
  }
  if (iequals(ext, ".jpg")) {
    return "image/jpeg";
  }
  if (iequals(ext, ".gif")) {
    return "image/gif";
  }
  if (iequals(ext, ".bmp")) {
    return "image/bmp";
  }
  if (iequals(ext, ".ico")) {
    return "image/vnd.microsoft.icon";
  }
  if (iequals(ext, ".tiff")) {
    return "image/tiff";
  }
  if (iequals(ext, ".tif")) {
    return "image/tiff";
  }
  if (iequals(ext, ".svg")) {
    return "image/svg+xml";
  }
  if (iequals(ext, ".svgz")) {
    return "image/svg+xml";
  }
  return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
inline std::string path_cat(boost::beast::string_view base,
                            boost::beast::string_view path) {
  if (base.empty()) {
    return std::string(path);
  }
  std::string result(base);
  char constexpr path_separator = '/';
  if (result.back() == path_separator) {
    result.resize(result.size() - 1);
  }
  result.append(path.data(), path.size());
  return result;
}

template <class Body, class Allocator>
inline boost::beast::http::response<boost::beast::http::dynamic_body>
bad_request(const boost::beast::http::request<
                Body, boost::beast::http::basic_fields<Allocator>> &req,
            boost::beast::string_view why) {
  boost::beast::http::response<boost::beast::http::dynamic_body> res{
      boost::beast::http::status::bad_request, req.version()};
  res.set(boost::beast::http::field::server, server_agent());
  res.set(boost::beast::http::field::content_type, "text/plain");
  res.keep_alive(req.keep_alive());
  boost::beast::ostream(res.body()) << why;
  return res;
}

template <class Body, class Allocator>
inline boost::beast::http::response<boost::beast::http::dynamic_body>
not_found(const boost::beast::http::request<
          Body, boost::beast::http::basic_fields<Allocator>> &req) {
  boost::beast::http::response<boost::beast::http::dynamic_body> res{
      boost::beast::http::status::not_found, req.version()};
  res.set(boost::beast::http::field::server, server_agent());
  res.set(boost::beast::http::field::content_type, "text/plain");
  res.keep_alive(req.keep_alive());
  boost::beast::ostream(res.body())
      << "The resource '" + std::string(req.target()) + "' was not found.";
  return res;
}

template <class Body, class Allocator>
inline boost::beast::http::response<boost::beast::http::dynamic_body>
server_error(const boost::beast::http::request<
                 Body, boost::beast::http::basic_fields<Allocator>> &req,
             boost::beast::string_view what) {
  boost::beast::http::response<boost::beast::http::dynamic_body> res{
      boost::beast::http::status::internal_server_error, req.version()};
  res.set(boost::beast::http::field::server, server_agent());
  res.set(boost::beast::http::field::content_type, "text/plain");
  res.keep_alive(req.keep_alive());
  boost::beast::ostream(res.body())
      << "An error occurred: '" + std::string(what) + "'";
  return res;
};

template <typename... T> struct overload final : T... {
  using T::operator()...;
  constexpr explicit overload(T... ts) : T(std::move(ts))... {}
};
template <typename... T> overload(T...) -> overload<T...>;

} // namespace cpp_http::server
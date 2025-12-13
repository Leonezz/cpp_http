#pragma once
#include "boost/asio/ip/tcp.hpp"
#include "boost/asio/spawn.hpp"
#include "boost/beast/core/tcp_stream.hpp"
#include "boost/outcome/result.hpp"
#include "boost/outcome/success_failure.hpp"
#include "request_builder.hpp"
#include "response.hpp"
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/url/scheme.hpp>
#include <boost/url/url_view.hpp>
#include <memory>
#include <openssl/err.h>
#include <openssl/tls1.h>
#include <string>
#include <utility>
namespace cpp_http::client {
inline boost::asio::ssl::context &get_ssl_context() {
  static boost::asio::ssl::context context{
      boost::asio::ssl::context::tls_client};
  static bool initialized = false;
  if (!initialized) {
    context.set_default_verify_paths();
    context.set_verify_mode(boost::asio::ssl::verify_peer);
    initialized = true;
  }
  return context;
}

inline std::string_view user_agent() { return "cpp-http/client"; }

template <class Response, class Request>
inline boost::outcome_v2::result<std::unique_ptr<incoming_response<Response>>>
send_http(http_request<Request> req, boost::asio::ip::tcp::resolver &resolver,
          uint64_t redirect_count, boost::asio::yield_context yield);

template <class Response, class Request>
inline boost::outcome_v2::result<std::unique_ptr<incoming_response<Response>>>
send_https(http_request<Request> req, boost::asio::ip::tcp::resolver &resolver,
           uint64_t redirect_count, boost::asio::yield_context yield);

template <class Response, class Request>
inline boost::outcome_v2::result<std::unique_ptr<incoming_response<Response>>>
send(http_request<Request> req, boost::asio::ip::tcp::resolver &resolver,
     uint64_t redirect_count, boost::asio::yield_context yield) {
  const auto &url = req.url;
  if (url.scheme_id() == boost::urls::scheme::https) {
    return send_https<Response, Request>(std::move(req), resolver,
                                         redirect_count, yield);
  }
  return send_http<Response, Request>(std::move(req), resolver, redirect_count,
                                      yield);
}

inline boost::outcome_v2::result<boost::asio::ip::tcp::resolver::results_type>
resolve(boost::urls::url_view url, boost::asio::ip::tcp::resolver &resolver,
        boost::asio::yield_context yield) {
  boost::system::error_code ec;
  std::string host = url.host_address();
  bool is_https = url.scheme_id() == boost::urls::scheme::https;
  auto port = std::to_string(url.port_number() > 0 ? url.port_number()
                             : is_https            ? 443
                                                   : 80);

  auto endpoints = resolver.async_resolve(host, port, yield[ec]);
  if (ec) {
    return ec;
  }
  return endpoints;
}

template <class Response, class Request>
inline boost::outcome_v2::result<std::unique_ptr<incoming_response<Response>>>
send_http(http_request<Request> req, boost::asio::ip::tcp::resolver &resolver,
          uint64_t redirect_count, boost::asio::yield_context yield) {
  boost::beast::error_code ec;
  auto stream =
      std::make_unique<boost::beast::tcp_stream>(yield.get_executor());
  if (req.timeout_ms > 0) {
    stream->expires_after(std::chrono::milliseconds(req.timeout_ms));
  }
  auto url = req.url;
  auto endpoints = resolve(url, resolver, yield);
  if (endpoints.has_error()) {
    return endpoints.error();
  }
  stream->async_connect(endpoints.value(), yield[ec]);
  if (ec) {
    return ec;
  }

  req.request.target(url.encoded_target());
  req.request.set(boost::beast::http::field::host, url.host_address());
  req.request.set(boost::beast::http::field::user_agent, user_agent());
  boost::beast::http::async_write(*stream, req.request, yield[ec]);
  if (ec) {
    return ec;
  }

  auto resp = std::make_unique<incoming_response<Response>>(std::move(stream));
  if (auto init_result = resp->init_parser(yield[ec]);
      init_result.has_error()) {
    return init_result.error();
  }
  if (resp->is_redirection() && req.auto_redirect) {
    auto loc = resp->redirect_url();
    if (!loc) {
      return boost::beast::http::error::bad_field;
    }
    if (redirect_count > req.max_redirects) {
      return boost::beast::errc::protocol_error;
    }
    req.url = *loc;
    return send<Response, Request>(std::move(req), resolver, redirect_count + 1,
                                   yield);
    // continue to next loop to redirect
  }
  return boost::outcome_v2::success(std::move(resp));
}

template <class Response, class Request>
boost::outcome_v2::
    result<std::unique_ptr<incoming_response<Response>>> inline send_https(
        http_request<Request> req, boost::asio::ip::tcp::resolver &resolver,
        uint64_t redirect_count, boost::asio::yield_context yield) {
  boost::beast::error_code ec;
  auto url = req.url;
  auto &&endpoints = resolve(url, resolver, yield);
  if (endpoints.has_error()) {
    return endpoints.error();
  }

  const auto host = url.host_address();
  auto ssl_stream =
      std::make_unique<boost::asio::ssl::stream<boost::beast::tcp_stream>>(
          yield.get_executor(), get_ssl_context());
  if (!SSL_set_tlsext_host_name(ssl_stream->native_handle(), host.c_str())) {
    ec.assign(static_cast<int>(::ERR_get_error()),
              boost::asio::error::get_ssl_category());
    return ec;
  }
  ssl_stream->set_verify_callback(
      boost::asio::ssl::host_name_verification(host));
  if (ec) {
    return ec;
  }

  if (req.timeout_ms > 0) {
    boost::beast::get_lowest_layer(*ssl_stream)
        .expires_after(std::chrono::milliseconds(req.timeout_ms));
  }
  boost::beast::get_lowest_layer(*ssl_stream)
      .async_connect(endpoints.value(), yield[ec]);
  if (ec) {
    return ec;
  }

  if (req.timeout_ms > 0) {
    boost::beast::get_lowest_layer(*ssl_stream)
        .expires_after(std::chrono::milliseconds(req.timeout_ms));
  }
  ssl_stream->async_handshake(boost::asio::ssl::stream_base::client, yield[ec]);
  if (ec) {
    return ec;
  }

  req.request.target(url.encoded_target());
  req.request.set(boost::beast::http::field::host, host);
  req.request.set(boost::beast::http::field::user_agent, user_agent());
  if (req.timeout_ms > 0) {
    boost::beast::get_lowest_layer(*ssl_stream)
        .expires_after(std::chrono::milliseconds(req.timeout_ms));
  }
  boost::beast::http::async_write(*ssl_stream, req.request, yield[ec]);
  if (ec) {
    return ec;
  }

  auto resp = std::make_unique<incoming_response<Response>>(std::move(ssl_stream));
  if (auto init_result = resp->init_parser(yield); init_result.has_error()) {
    return init_result.error();
  }
  if (resp->is_redirection() && req.auto_redirect) {
    auto loc = resp->redirect_url();
    if (!loc) {
      return boost::beast::http::error::bad_field;
    }
    if (redirect_count > req.max_redirects) {
      return boost::beast::errc::protocol_error;
    }
    req.url = *loc;
    return send<Response, Request>(std::move(req), resolver, redirect_count + 1,
                                   yield);
    // continue to next loop to redirect
  }
  return boost::outcome_v2::success(std::move(resp));
}

template <class Response, class Request>
boost::outcome_v2::result<std::unique_ptr<incoming_response<Response>>> inline send(
    http_request<Request> req, boost::asio::yield_context yield) {
  auto resolver = boost::asio::ip::tcp::resolver(yield.get_executor());
  return send<Response, Request>(req, resolver, 0, yield);
}
} // namespace cpp_http::client
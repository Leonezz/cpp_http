#pragma once
#include "server/matcher.hpp"
#include "server/request.hpp"
#include "server/response.hpp"
#include "server/service.hpp"
#include "server/util.hpp"
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/string_type.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <boost/json.hpp>
#include <boost/json/parse.hpp>
#include <boost/url.hpp>
#include <boost/variant2/variant.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace cpp_http::server {
// Report a failure
void fail(boost::beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

class server {
  boost::asio::ip::tcp::endpoint endpoint_;
  // Handles an HTTP server connection
  std::vector<std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
      get_services_;
  std::vector<std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
      post_services_;
  std::vector<std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
      head_services_;
  std::vector<std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
      put_services_;
  std::vector<std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
      delete_services_;
  std::vector<std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
      options_services_;

  inline response dispatch_request(request &&req,
                                            boost::asio::yield_context yield) {
    auto method = req.request_cref().method();
    static std::vector<
        std::pair<std::unique_ptr<matcher>, std::unique_ptr<service>>>
        empty_matchers_;
    const auto &matchers = [&]() -> const auto & {
      switch (method) {
      case boost::beast::http::verb::get:
        return get_services_;
      case boost::beast::http::verb::post:
        return post_services_;
      case boost::beast::http::verb::head:
        return head_services_;
      case boost::beast::http::verb::put:
        return put_services_;
      case boost::beast::http::verb::delete_:
        return delete_services_;
      case boost::beast::http::verb::options:
        return options_services_;
      default:
        return empty_matchers_;
      }
    }();
    auto request = req.request_cref();
    for (const auto &matcher : matchers) {
      if (matcher.first->match(req)) {
        auto res = matcher.second->handle_request(std::move(req), yield);
        if (res.has_error()) {
          return response{
              server_error(request, res.error().to_string())};
        }
        return std::move(res).value();
      }
    }
    return response{not_found(request)};
  }

  boost::outcome_v2::result<void>
  do_session(boost::asio::ip::tcp::socket socket,
             boost::asio::yield_context yield) {
    boost::beast::tcp_stream stream(std::move(socket));
    boost::beast::error_code ec;

    // This buffer is required to persist across reads
    boost::beast::flat_buffer buffer;

    // This lambda is used to send messages
    for (;;) {
      // Set the timeout.
      stream.expires_after(std::chrono::seconds(30));

      // Read a request
      boost::beast::http::request<boost::beast::http::string_body> req;
      boost::beast::http::async_read(stream, buffer, req, yield[ec]);
      stream.expires_never();
      if (ec) {
        break;
      }

      auto request_wrapper = request(std::move(req));
      auto response = dispatch_request(std::move(request_wrapper), yield[ec]);
      if (ec) {
        break;
      }
      bool is_chunked = false;
      bool keep_alive =
          response.header_cref()[boost::beast::http::field::connection] ==
          "keep-alive";
      std::move(response).async_write(stream, yield[ec]);
      if (ec) {
        std::cout << "exit with error: " << ec.message() << "\n";
        break;
      }
      if (!keep_alive) {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        break;
      }
    }
    std::cout << "socket end\n";
    // Send a TCP shutdown
    // auto _ = stream_.socket().shutdown(
    //     boost::asio::ip::tcp::socket::shutdown_send, ec);

    return boost::outcome_v2::success();
  }

  void do_listen(boost::asio::ip::tcp::endpoint endpoint,
                 boost::asio::yield_context yield) {
    boost::beast::error_code ec;

    // Open the acceptor
    boost::asio::ip::tcp::acceptor acceptor(yield.get_executor());
    auto _ = acceptor.open(endpoint.protocol(), ec);
    if (ec) {
      return fail(ec, "open");
    }

    // Allow address reuse
    auto _ =
        acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
      return fail(ec, "set_option");
    }

    // Bind to the server address
    auto _ = acceptor.bind(endpoint, ec);
    if (ec) {
      return fail(ec, "bind");
    }

    // Start listening for connections
    auto _ =
        acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
      return fail(ec, "listen");
    }

    for (;;) {
      boost::asio::ip::tcp::socket socket(yield.get_executor());
      acceptor.async_accept(socket, yield[ec]);
      if (ec) {
        fail(ec, "accept");
      } else {
        boost::asio::spawn(
            yield.get_executor(),
            [socket = std::move(socket),
             this](boost::asio::yield_context yield) mutable {
              auto res = do_session(std::move(socket), yield);
            },
            // we ignore the result of the session,
            // most errors are handled with error_code
            boost::asio::detached);
      }
    }
  }

public:
  explicit server(boost::asio::ip::tcp::endpoint endpoint)
      : endpoint_(std::move(endpoint)) {}
  ~server() = default;

  inline void run(boost::asio::yield_context yield) {
    do_listen(endpoint_, yield);
  }

  inline void register_service(boost::beast::http::verb method,
                               std::unique_ptr<matcher> matcher,
                               std::unique_ptr<service> service) {
    switch (method) {
    case boost::beast::http::verb::get:
      get_services_.emplace_back(std::move(matcher), std::move(service));
      break;
    case boost::beast::http::verb::post:
      post_services_.emplace_back(std::move(matcher), std::move(service));
      break;
    case boost::beast::http::verb::head:
      head_services_.emplace_back(std::move(matcher), std::move(service));
      break;
    case boost::beast::http::verb::put:
      put_services_.emplace_back(std::move(matcher), std::move(service));
      break;
    case boost::beast::http::verb::delete_:
      delete_services_.emplace_back(std::move(matcher), std::move(service));
      break;
    case boost::beast::http::verb::options:
      options_services_.emplace_back(std::move(matcher), std::move(service));
      break;
    default:
      break;
    }
  }

  inline void get(const std::string &pattern,
                  std::unique_ptr<service> service) {
    register_service(boost::beast::http::verb::get, make_matcher(pattern),
                     std::move(service));
  }

  inline void post(const std::string &pattern,
                   std::unique_ptr<service> service) {
    register_service(boost::beast::http::verb::post, make_matcher(pattern),
                     std::move(service));
  }

  inline void head(const std::string &pattern,
                   std::unique_ptr<service> service) {
    register_service(boost::beast::http::verb::head, make_matcher(pattern),
                     std::move(service));
  }

  inline void put(const std::string &pattern,
                  std::unique_ptr<service> service) {
    register_service(boost::beast::http::verb::put, make_matcher(pattern),
                     std::move(service));
  }

  inline void delete_(const std::string &pattern,
                      std::unique_ptr<service> service) {
    register_service(boost::beast::http::verb::delete_, make_matcher(pattern),
                     std::move(service));
  }

  inline void options(const std::string &pattern,
                      std::unique_ptr<service> service) {
    register_service(boost::beast::http::verb::options, make_matcher(pattern),
                     std::move(service));
  }
};
} // namespace cpp_http::server
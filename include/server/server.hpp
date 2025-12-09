#pragma once
#include "server/matcher.hpp"
#include "server/request.hpp"
#include "server/session.hpp"
#include "server/util.hpp"
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
#include <boost/json.hpp>
#include <boost/json/parse.hpp>
#include <boost/url.hpp>
#include <boost/variant2/variant.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace cpp_http::server {

// Return a response for the given request.
//
// The concrete type of the response message (which depends on the
// request), is type-erased in message_generator.
// template <class Body, class Allocator>
// boost::beast::http::message_generator
// handle_request(boost::beast::string_view doc_root,
//                boost::beast::http::request<
//                    Body, boost::beast::http::basic_fields<Allocator>> &&req)
//                    {

//   // Returns a server error response

//   // Make sure we can handle the method
//   if (req.method() != boost::beast::http::verb::get &&
//       req.method() != boost::beast::http::verb::head) {
//     return bad_request(req, "Unknown HTTP-method");
//   }

//   // Request path must be absolute and not contain "..".
//   if (req.target().empty() || req.target()[0] != '/' ||
//       req.target().find("..") != boost::beast::string_view::npos) {
//     return bad_request(req, "Illegal request-target");
//   }

//   // Build the path to the requested file
//   std::string path = path_cat(doc_root, req.target());
//   if (req.target().back() == '/') {
//     path.append("index.html");
//   }

//   // Attempt to open the file
//   boost::beast::error_code ec;
//   boost::beast::http::file_body::value_type body;
//   body.open(path.c_str(), boost::beast::file_mode::scan, ec);

//   // Handle the case where the file doesn't exist
//   if (ec == boost::beast::errc::no_such_file_or_directory) {
//     return not_found(req);
//   }

//   // Handle an unknown error
//   if (ec) {
//     return server_error(req, ec.message());
//   }

//   // Cache the size since we need it after the move
//   auto const size = body.size();

//   // Respond to HEAD request
//   if (req.method() == boost::beast::http::verb::head) {
//     boost::beast::http::response<boost::beast::http::empty_body> res{
//         boost::beast::http::status::ok, req.version()};
//     res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//     res.set(boost::beast::http::field::content_type, mime_type(path));
//     res.content_length(size);
//     res.keep_alive(req.keep_alive());
//     return res;
//   }

//   // Respond to GET request
//   boost::beast::http::response<boost::beast::http::file_body> res{
//       std::piecewise_construct, std::make_tuple(std::move(body)),
//       std::make_tuple(boost::beast::http::status::ok, req.version())};
//   res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//   res.set(boost::beast::http::field::content_type, mime_type(path));
//   res.content_length(size);
//   res.keep_alive(req.keep_alive());
//   return res;
// }

//------------------------------------------------------------------------------

// Report a failure
void fail(boost::beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

class server {
  boost::asio::ip::tcp::endpoint endpoint_;
  // Handles an HTTP server connection
  using session_maker_t =
      std::function<std::unique_ptr<session>(request &&req)>;
  using simple_session_handler_t = std::function<boost::outcome_v2::result<
      boost::beast::http::response<boost::beast::http::dynamic_body>>(
      request &&req, boost::asio::yield_context yield)>;

  std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
      get_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
      simple_get_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
      post_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
      simple_post_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
      head_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
      simple_head_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
      put_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
      simple_put_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
      delete_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
      simple_delete_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
      options_matchers_;
  std::vector<std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
      simple_options_matchers_;

  inline boost::beast::http::response<boost::beast::http::dynamic_body>
  dispatch_simple_request(request &&req, boost::asio::yield_context yield) {
    auto method = req.request_cref().method();
    static std::vector<
        std::pair<std::unique_ptr<matcher>, simple_session_handler_t>>
        empty_matchers_;
    const auto &matchers = [&]() -> const auto & {
      switch (method) {
      case boost::beast::http::verb::get:
        return simple_get_matchers_;
      case boost::beast::http::verb::post:
        return simple_post_matchers_;
      case boost::beast::http::verb::head:
        return simple_head_matchers_;
      case boost::beast::http::verb::put:
        return simple_put_matchers_;
      case boost::beast::http::verb::delete_:
        return simple_delete_matchers_;
      case boost::beast::http::verb::options:
        return simple_options_matchers_;
      default:
        return empty_matchers_;
      }
    }();
    auto request = req.request_cref();
    for (const auto &matcher : matchers) {
      if (matcher.first->match(req)) {
        auto res = matcher.second(std::move(req), yield);
        if (res.has_error()) {
          return server_error(request, res.error().to_string());
        }
        return res.value();
      }
    }
    return not_found(request);
  }
  std::optional<session_maker_t> find_session_maker(request &req) {
    auto method = req.request_cref().method();
    static std::vector<std::pair<std::unique_ptr<matcher>, session_maker_t>>
        empty_matchers_;
    const auto &matchers = [&]() -> const auto & {
      switch (method) {
      case boost::beast::http::verb::get:
        return get_matchers_;
      case boost::beast::http::verb::post:
        return post_matchers_;
      case boost::beast::http::verb::head:
        return head_matchers_;
      case boost::beast::http::verb::put:
        return put_matchers_;
      case boost::beast::http::verb::delete_:
        return delete_matchers_;
      case boost::beast::http::verb::options:
        return options_matchers_;
      default:
        return empty_matchers_;
      }
    }();
    for (const auto &matcher : matchers) {
      if (matcher.first->match(req)) {
        return matcher.second;
      }
    }
    return {};
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
      auto session_maker = find_session_maker(request_wrapper);
      if (!session_maker.has_value()) {
        auto response =
            dispatch_simple_request(std::move(request_wrapper), yield);
        boost::beast::async_write(
            stream, boost::beast::http::message_generator(std::move(response)),
            yield[ec]);
        break;
      }

      auto sess = session_maker.value()(std::move(request_wrapper));

      auto ch = std::make_shared<boost::asio::experimental::channel<void(
          boost::beast::error_code,
          std::variant<
              boost::beast::http::response<boost::beast::http::dynamic_body>,
              boost::beast::flat_buffer>)>>(yield.get_executor(), 10);
      bool is_chunked = false;
      // Handle the request
      boost::asio::spawn(
          yield.get_executor(),
          [this, tx = ch, session = std::move(sess)](
              boost::asio::yield_context yield) mutable {
            session->handle_request(*tx, yield);
          },
          [tx = ch](auto &) {
            tx->cancel();
            tx->close();
          });
      // Determine if we should close the connection
      uint64_t message_sent = 0;
      uint64_t message_received = 0;
      bool keep_alive = false;
      while (ch->is_open()) {
        auto message = ch->async_receive(yield[ec]);
        if (ec) {
          if (message_sent == 0 && stream.socket().is_open()) {
            // if no message sent, send an error response
            auto response =
                server_error(request_wrapper.request_cref(),
                             "Error in handling request: " + ec.message());
            boost::beast::async_write(
                stream,
                boost::beast::http::message_generator(std::move(response)),
                yield[ec]);
          }
          break;
        }
        message_received++;
        std::visit(
            overload{
                [&stream, yield, &ec, &keep_alive,
                 &is_chunked](boost::beast::http::response<
                              boost::beast::http::dynamic_body>
                                  msg) {
                  keep_alive = msg.keep_alive();
                  is_chunked = msg.chunked();
                  if (is_chunked) {
                    boost::beast::http::response<boost::beast::http::empty_body>
                        response;
                    response.result(msg.result());
                    response.reason(msg.reason());
                    for (auto it = msg.begin(); it != msg.end(); ++it) {
                      response.set(it->name(), it->value());
                    }
                    response.chunked(true);
                    std::cout << "send msg: " << response << "\n";
                    boost::beast::http::response_serializer<
                        boost::beast::http::empty_body>
                        serializer(response);
                    boost::beast::http::async_write_header(stream, serializer,
                                                           yield[ec]);
                  } else {
                    boost::beast::async_write(
                        stream,
                        boost::beast::http::message_generator(std::move(msg)),
                        yield[ec]);
                  }
                  if (ec) {
                    std::cout << "send msg error: " << ec.message() << "\n";
                  }
                },
                [&stream, yield, &ec,
                 &is_chunked](boost::beast::flat_buffer buf) {
                  if (is_chunked) {
                    boost::asio::async_write(
                        stream, boost::beast::http::make_chunk(buf.data()),
                        yield[ec]);
                  } else {
                    boost::asio::async_write(stream, buf, yield[ec]);
                  }
                  if (ec) {
                    std::cout << "send chunk error: " << ec.message() << "\n";
                  }
                }},
            std::move(message));
        message_sent++;
        if (ec) {
          std::cout << "exit with error: " << ec.message() << "\n";
          break;
        }
      }

      std::cout << "send msg end\n";
      if (ec) {
        break;
      }

      if (is_chunked && stream.socket().is_open()) {
        boost::asio::async_write(stream, boost::beast::http::make_chunk_last(),
                                 yield[ec]);
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
  server(boost::asio::ip::tcp::endpoint endpoint)
      : endpoint_(std::move(endpoint)) {}

  void run(boost::asio::yield_context yield) { do_listen(endpoint_, yield); }

  void get(const std::string &pattern, session_maker_t session_maker) {
    get_matchers_.emplace_back(make_matcher(pattern), std::move(session_maker));
  }
  template <typename Handler>
  void simple_get(const std::string &pattern, Handler session_maker) {
    simple_get_matchers_.emplace_back(make_matcher(pattern),
                                      std::move(session_maker));
  }
  void post(const std::string &pattern, session_maker_t session_maker) {
    post_matchers_.emplace_back(make_matcher(pattern),
                                std::move(session_maker));
  }
  void simple_post(const std::string &pattern,
                   simple_session_handler_t session_maker) {
    simple_post_matchers_.emplace_back(make_matcher(pattern),
                                       std::move(session_maker));
  }
  void head(const std::string &pattern, session_maker_t session_maker) {
    head_matchers_.emplace_back(make_matcher(pattern),
                                std::move(session_maker));
  }
  void simple_head(const std::string &pattern,
                   simple_session_handler_t session_maker) {
    simple_head_matchers_.emplace_back(make_matcher(pattern),
                                       std::move(session_maker));
  }
  void put(const std::string &pattern, session_maker_t session_maker) {
    put_matchers_.emplace_back(make_matcher(pattern), std::move(session_maker));
  }
  void simple_put(const std::string &pattern,
                  simple_session_handler_t session_maker) {
    simple_put_matchers_.emplace_back(make_matcher(pattern),
                                      std::move(session_maker));
  }
  void delete_(const std::string &pattern, session_maker_t session_maker) {
    delete_matchers_.emplace_back(make_matcher(pattern),
                                  std::move(session_maker));
  }
  void simple_delete(const std::string &pattern,
                     simple_session_handler_t session_maker) {
    simple_delete_matchers_.emplace_back(make_matcher(pattern),
                                         std::move(session_maker));
  }
  void options(const std::string &pattern, session_maker_t session_maker) {
    options_matchers_.emplace_back(make_matcher(pattern),
                                   std::move(session_maker));
  }
  void simple_options(const std::string &pattern,
                      simple_session_handler_t session_maker) {
    simple_options_matchers_.emplace_back(make_matcher(pattern),
                                          std::move(session_maker));
  }
};
} // namespace cpp_http::server
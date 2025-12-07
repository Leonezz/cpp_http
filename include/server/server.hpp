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
#include <boost/json/parse.hpp>
#include <boost/variant2/variant.hpp>
#include <boost/json.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace cpp_http::server {
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
inline boost::beast::http::message_generator
bad_request(const boost::beast::http::request<
                Body, boost::beast::http::basic_fields<Allocator>> &req,
            boost::beast::string_view why) {
  boost::beast::http::response<boost::beast::http::string_body> res{
      boost::beast::http::status::bad_request, req.version()};
  res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(boost::beast::http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = std::string(why);
  res.prepare_payload();
  return res;
}

template <class Body, class Allocator>
inline boost::beast::http::message_generator
not_found(const boost::beast::http::request<
              Body, boost::beast::http::basic_fields<Allocator>> &req) {
  boost::beast::http::response<boost::beast::http::string_body> res{
      boost::beast::http::status::not_found, req.version()};
  res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(boost::beast::http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = "The resource '" + std::string(req.target()) + "' was not found.";
  res.prepare_payload();
  return res;
}

template <class Body, class Allocator>
inline boost::beast::http::message_generator
server_error(const boost::beast::http::request<
                 Body, boost::beast::http::basic_fields<Allocator>> &req,
             boost::beast::string_view what) {
  boost::beast::http::response<boost::beast::http::string_body> res{
      boost::beast::http::status::internal_server_error, req.version()};
  res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(boost::beast::http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = "An error occurred: '" + std::string(what) + "'";
  res.prepare_payload();
  return res;
};

// Return a response for the given request.
//
// The concrete type of the response message (which depends on the
// request), is type-erased in message_generator.
template <class Body, class Allocator>
boost::beast::http::message_generator
handle_request(boost::beast::string_view doc_root,
               boost::beast::http::request<
                   Body, boost::beast::http::basic_fields<Allocator>> &&req) {

  // Returns a server error response

  // Make sure we can handle the method
  if (req.method() != boost::beast::http::verb::get &&
      req.method() != boost::beast::http::verb::head) {
    return bad_request(req, "Unknown HTTP-method");
  }

  // Request path must be absolute and not contain "..".
  if (req.target().empty() || req.target()[0] != '/' ||
      req.target().find("..") != boost::beast::string_view::npos) {
    return bad_request(req, "Illegal request-target");
  }

  // Build the path to the requested file
  std::string path = path_cat(doc_root, req.target());
  if (req.target().back() == '/') {
    path.append("index.html");
  }

  // Attempt to open the file
  boost::beast::error_code ec;
  boost::beast::http::file_body::value_type body;
  body.open(path.c_str(), boost::beast::file_mode::scan, ec);

  // Handle the case where the file doesn't exist
  if (ec == boost::beast::errc::no_such_file_or_directory) {
    return not_found(req);
  }

  // Handle an unknown error
  if (ec) {
    return server_error(req, ec.message());
  }

  // Cache the size since we need it after the move
  auto const size = body.size();

  // Respond to HEAD request
  if (req.method() == boost::beast::http::verb::head) {
    boost::beast::http::response<boost::beast::http::empty_body> res{
        boost::beast::http::status::ok, req.version()};
    res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(boost::beast::http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return res;
  }

  // Respond to GET request
  boost::beast::http::response<boost::beast::http::file_body> res{
      std::piecewise_construct, std::make_tuple(std::move(body)),
      std::make_tuple(boost::beast::http::status::ok, req.version())};
  res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(boost::beast::http::field::content_type, mime_type(path));
  res.content_length(size);
  res.keep_alive(req.keep_alive());
  return res;
}

//------------------------------------------------------------------------------

// Report a failure
void fail(boost::beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

// Handles an HTTP server connection
void do_session(boost::beast::tcp_stream &stream,
                std::shared_ptr<std::string const> const &doc_root,
                boost::asio::yield_context yield) {
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
    if (ec == boost::beast::http::error::end_of_stream) {
      break;
    }
    if (ec) {
      return fail(ec, "read");
    }

    // Handle the request
    boost::beast::http::message_generator msg =
        handle_request(*doc_root, std::move(req));

    // Determine if we should close the connection
    bool keep_alive = msg.keep_alive();

    // Send the response
    boost::beast::async_write(stream, std::move(msg), yield[ec]);

    if (ec) {
      return fail(ec, "write");
    }

    if (!keep_alive) {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      break;
    }
  }

  // Send a TCP shutdown
  auto _ =
      stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);

  // At this point the connection is closed gracefully
}

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
void do_listen(boost::asio::io_context &ioc,
               boost::asio::ip::tcp::endpoint endpoint,
               std::shared_ptr<std::string const> const &doc_root,
               boost::asio::yield_context yield) {
  boost::beast::error_code ec;

  // Open the acceptor
  boost::asio::ip::tcp::acceptor acceptor(ioc);
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
    boost::asio::ip::tcp::socket socket(ioc);
    acceptor.async_accept(socket, yield[ec]);
    if (ec) {
      fail(ec, "accept");
    } else
      boost::asio::spawn(acceptor.get_executor(),
                         std::bind(&do_session,
                                   boost::beast::tcp_stream(std::move(socket)),
                                   doc_root, std::placeholders::_1),
                         // we ignore the result of the session,
                         // most errors are handled with error_code
                         boost::asio::detached);
  }
}

} // namespace cpp_http::server
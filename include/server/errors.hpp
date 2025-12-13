#pragma once
#include <boost/outcome/result.hpp>
#include <boost/system/detail/error_category.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/is_error_code_enum.hpp>
#include <type_traits>
namespace cpp_http::server {
namespace outcome = boost::outcome_v2;
template <typename T> using result = boost::outcome_v2::result<T>;
enum class server_error_code {
  bad_request = 0,
  not_found = 1,
  bad_accept = 2,
  bad_accept_encoding = 3,
};

class server_error_category : public boost::system::error_category {
public:
  virtual ~server_error_category() = default;
  const char *name() const noexcept override { return "server side error"; }

  std::string message(int ev) const override {
    switch (static_cast<server_error_code>(ev)) {
    case cpp_http::server::server_error_code::bad_request:
      return "Bad Request";
    case cpp_http::server::server_error_code::not_found:
      return "Not Found";
    case cpp_http::server::server_error_code::bad_accept:
      return "Bad Accept Header";
    case cpp_http::server::server_error_code::bad_accept_encoding:
      return "Bad Accept-Encoding Header";
    }
  }
};

inline const boost::system::error_category &server_error_category_instance() {
  static server_error_category instance;
  return instance;
}

inline auto make_error_code(enum server_error_code e) {
  return boost::system::error_code(static_cast<int>(e),
                                   server_error_category_instance());
}
}; // namespace cpp_http::server

namespace boost::system {
template <>
struct is_error_code_enum<cpp_http::server::server_error_code> : std::true_type {};
} // namespace boost::system
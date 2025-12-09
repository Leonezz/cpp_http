#pragma once
#include <optional>
#include <string>
namespace cpp_http {
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
} // namespace cpp_http
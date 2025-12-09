#include "server/request.hpp"
#include <boost/beast/http.hpp>
#include <boost/url.hpp>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
namespace cpp_http::server {
class matcher {
public:
  explicit matcher(std::string pattern) : pattern_(std::move(pattern)) {}
  virtual ~matcher() = default;

  const std::string &pattern() const { return pattern_; }

  // Match request path and populate its matches and
  virtual bool match(request &request) const = 0;

private:
  std::string pattern_;
};

/**
 * Captures parameters in request path and stores them in Request::path_params
 *
 * Capture name is a substring of a pattern from : to /.
 * The rest of the pattern is matched against the request path directly
 * Parameters are captured starting from the next character after
 * the end of the last matched static pattern fragment until the next /.
 *
 * Example pattern:
 * "/path/fragments/:capture/more/fragments/:second_capture"
 * Static fragments:
 * "/path/fragments/", "more/fragments/"
 *
 * Given the following request path:
 * "/path/fragments/:1/more/fragments/:2"
 * the resulting capture will be
 * {{"capture", "1"}, {"second_capture", "2"}}
 */
class path_params_matcher final : public matcher {
public:
  explicit path_params_matcher(std::string pattern)
      : matcher(std::move(pattern)) {
    constexpr std::string_view marker = "/:";

    // One past the last ending position of a path param substring
    std::size_t last_param_end = 0;
    std::unordered_set<std::string> param_name_set;

    while (true) {
      const auto marker_pos = pattern.find(
          marker, last_param_end == 0 ? last_param_end : last_param_end - 1);
      if (marker_pos == std::string::npos) {
        break;
      }

      static_fragments_.push_back(
          pattern.substr(last_param_end, marker_pos - last_param_end + 1));

      const auto param_name_start = marker_pos + marker.size();

      auto sep_pos = pattern.find(separator, param_name_start);
      if (sep_pos == std::string::npos) {
        sep_pos = pattern.length();
      }

      auto param_name =
          pattern.substr(param_name_start, sep_pos - param_name_start);

      if (param_name_set.find(param_name) != param_name_set.cend()) {
        std::string msg = "Encountered path parameter '" + param_name +
                          "' multiple times in route pattern '" + pattern +
                          "'.";
        std::cerr << msg << '\n';
      }

      param_names_.push_back(std::move(param_name));

      last_param_end = sep_pos + 1;
    }

    if (last_param_end < pattern.length()) {
      static_fragments_.push_back(pattern.substr(last_param_end));
    }
  }

  bool match(request &request) const override {
    request.matches_ref() = std::smatch();
    request.path_params_ref().clear();
    request.path_params_ref().reserve(param_names_.size());

    const auto &path = request.path_cref();
    // One past the position at which the path matched the pattern last time
    std::size_t starting_pos = 0;
    for (size_t i = 0; i < static_fragments_.size(); ++i) {
      const auto &fragment = static_fragments_[i];

      if (starting_pos + fragment.length() > path.length()) {
        return false;
      }

      // Avoid unnecessary allocation by using strncmp instead of substr +
      // comparison
      if (std::string_view{path}.substr(starting_pos) != fragment) {
        return false;
      }

      starting_pos += fragment.length();

      // Should only happen when we have a static fragment after a param
      // Example: '/users/:id/subscriptions'
      // The 'subscriptions' fragment here does not have a corresponding param
      if (i >= param_names_.size()) {
        continue;
      }

      auto sep_pos = path.find(separator, starting_pos);
      if (sep_pos == std::string::npos) {
        sep_pos = path.length();
      }

      const auto &param_name = param_names_[i];
      request.path_params_ref().emplace(
          param_name, path.substr(starting_pos, sep_pos - starting_pos));

      // Mark everything up to '/' as matched
      starting_pos = sep_pos + 1;
    }
    // Returns false if the path is longer than the pattern
    return starting_pos >= path.length();
  }

private:
  // Treat segment separators as the end of path parameter capture
  // Does not need to handle query parameters as they are parsed before path
  // matching
  static constexpr char separator = '/';
  // Contains static path fragments to match against, excluding the '/' after
  // path params
  // Fragments are separated by path params
  std::vector<std::string> static_fragments_;
  // Stores the names of the path parameters to be used as keys in the
  // Request::path_params map
  std::vector<std::string> param_names_;
};

/**
 * Performs std::regex_match on request path
 * and stores the result in Request::matches
 *
 * Note that regex match is performed directly on the whole request.
 * This means that wildcard patterns may match multiple path segments with /:
 * "/begin/(.*)/end" will match both "/begin/middle/end" and "/begin/1/2/end".
 */
class regex_matcher final : public matcher {
public:
  explicit regex_matcher(const std::string &pattern)
      : matcher(pattern), regex_(pattern) {}

  bool match(request &request) const override {
    request.path_params_ref().clear();
    request.matches_ref() = std::smatch();
    return std::regex_match(request.path_cref(), request.matches_ref(), regex_);
  }

private:
  std::regex regex_;
};
inline std::unique_ptr<matcher> make_matcher(const std::string &pattern) {
  if (pattern.find("/:") != std::string::npos) {
    return std::make_unique<path_params_matcher>(pattern);
  }
  return std::make_unique<regex_matcher>(pattern);
}
} // namespace cpp_http::server
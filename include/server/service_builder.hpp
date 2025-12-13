#pragma once
#include "server/middleware.hpp"
#include "server/service.hpp"
#include <memory>
#include <utility>
namespace cpp_http::server {

class service_builder {
  std::unique_ptr<middleware> middleware_{
      std::make_unique<indentity_middleware>()};

public:
  service_builder() = default;
  ~service_builder() = default;

  service_builder &with_middleware(std::unique_ptr<middleware> middleware) {
    middleware_ = std::make_unique<stacked_middleware>(std::move(middleware_),
                                                       std::move(middleware));
    return *this;
  }

  template <typename F> service_builder &with_function_middleware(F &&func) {
    return with_middleware(
        std::make_unique<function_middleware>(std::forward<F>(func)));
  }

  template <typename F>
  service_builder &with_pre_request_middleware(F &&handler) {
    return with_middleware(
        std::make_unique<pre_request_middleware>(std::forward<F>(handler)));
  }

  template <typename F>
  service_builder &with_after_response_middleware(F &&handler) {
    return with_middleware(
        std::make_unique<after_response_middleware>(std::forward<F>(handler)));
  }

  std::unique_ptr<service> build_service(std::unique_ptr<service> service) && {
    return std::move(middleware_)->layer(std::move(service));
  }

  template <typename F>
  std::unique_ptr<service> build_function_service(F &&func) && {
    return std::move(*this).build_service(
        std::make_unique<function_service>(std::forward<F>(func)));
  }
};
} // namespace cpp_http::server
#pragma once
#include "server/service.hpp"
#include <memory>
#include <utility>
namespace cpp_http::server {
class middleware {
public:
  virtual ~middleware() = default;
  virtual std::unique_ptr<service> layer(std::unique_ptr<service> service) = 0;
};

class indentity_middleware : public middleware {
public:
  indentity_middleware() = default;
  ~indentity_middleware() override = default;
  std::unique_ptr<service> layer(std::unique_ptr<service> service) override {
    return service;
  }
};

class stacked_middleware : public middleware {
  std::unique_ptr<middleware> inner_;
  std::unique_ptr<middleware> outer_;

public:
  explicit stacked_middleware(std::unique_ptr<middleware> inner,
                              std::unique_ptr<middleware> outer)
      : inner_(std::move(inner)), outer_(std::move(outer)) {}
  ~stacked_middleware() override = default;
  std::unique_ptr<service> layer(std::unique_ptr<service> service) override {
    auto inner_service = inner_->layer(std::move(service));
    return outer_->layer(std::move(inner_service));
  }
};

class function_middleware : public middleware {
  using middle_ware_func_t =
      std::function<std::unique_ptr<service>(std::unique_ptr<service> service)>;

  middle_ware_func_t func_;

public:
  template <typename F>
  explicit constexpr function_middleware(F func) : func_(std::move(func)) {}
  std::unique_ptr<service> layer(std::unique_ptr<service> ser) override {
    return func_(std::move(ser));
  }
};

class pre_request_middleware : public middleware {
  using pre_request_handler_t =
      std::function<request(request &&, boost::asio::yield_context)>;
  pre_request_handler_t handler_;

public:
  template <typename F>
  explicit pre_request_middleware(F handler) : handler_(std::move(handler)) {}
  std::unique_ptr<service> layer(std::unique_ptr<service> service) override {
    return std::make_unique<pre_request_service>(std::move(handler_),
                                                 std::move(service));
  }
};

class after_response_middleware : public middleware {
  using after_response_handler_t = std::function<result<response>(
      result<response> &&, boost::asio::yield_context)>;
  after_response_handler_t handler_;

public:
  template <typename F>
  explicit after_response_middleware(F handler)
      : handler_(std::move(handler)) {}
  std::unique_ptr<service> layer(std::unique_ptr<service> service) override {
    return std::make_unique<after_response_service>(std::move(handler_),
                                                    std::move(service));
  }
};
} // namespace cpp_http::server
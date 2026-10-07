#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace jeff::daw {

using Id = std::string;
using Frame = std::int64_t;

enum class ErrorCode {
  invalidProject, invalidTimingMap, missingEntity, duplicateId, invalidCommand, cancelled,
  readFailure, writeFailure, storageLimit, decodeFailure
};

struct Error {
  ErrorCode code;
  std::string message;
};

template <typename T> class Result {
public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(ErrorCode code, std::string message) {
    return Result(Error{code, std::move(message)});
  }
  explicit operator bool() const { return std::holds_alternative<T>(data_); }
  const T& value() const { return std::get<T>(data_); }
  T& value() { return std::get<T>(data_); }
  const Error& error() const { return std::get<Error>(data_); }

private:
  explicit Result(T value) : data_(std::move(value)) {}
  explicit Result(Error error) : data_(std::move(error)) {}
  std::variant<T, Error> data_;
};

template <> class Result<void> {
public:
  static Result success() { return Result(); }
  static Result failure(ErrorCode code, std::string message) {
    return Result(Error{code, std::move(message)});
  }
  explicit operator bool() const { return !error_.has_value(); }
  const Error& error() const { return *error_; }

private:
  Result() = default;
  explicit Result(Error error) : error_(std::move(error)) {}
  std::optional<Error> error_;
};

class CancellationToken {
public:
  void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
  bool isCancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

private:
  std::atomic<bool> cancelled_{false};
};

} // namespace jeff::daw

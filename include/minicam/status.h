// Lightweight error handling in the style of absl::Status.
//
// The Google C++ Style Guide does not use exceptions, so every operation that
// can fail returns a Status (or a StatusOr<T> when it also produces a value).
// The same type later carries errors across pipeline threads.

#ifndef MINICAM_STATUS_H_
#define MINICAM_STATUS_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace minicam {

enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kFailedPrecondition,
  kOutOfRange,
  kDataLoss,
  kCancelled,
  kInternal,
};

std::string_view StatusCodeName(StatusCode code);

class [[nodiscard]] Status {
 public:
  // A default-constructed Status is OK.
  Status() = default;
  Status(StatusCode code, std::string message);

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  // "OK" or "<CODE>: <message>".
  std::string ToString() const;

 private:
  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

inline Status OkStatus() { return {}; }
Status InvalidArgumentError(std::string message);
Status NotFoundError(std::string message);
Status FailedPreconditionError(std::string message);
Status OutOfRangeError(std::string message);
Status DataLossError(std::string message);
Status CancelledError(std::string message);
Status InternalError(std::string message);

// Holds either a value of type T or a non-OK Status.
template <typename T>
class [[nodiscard]] StatusOr {
 public:
  // Implicit, like absl::StatusOr, so functions can `return value;` or
  // `return SomeError(...);`.
  StatusOr(Status status)  // NOLINT(google-explicit-constructor)
      : status_(std::move(status)) {
    if (status_.ok()) {
      status_ = InternalError("StatusOr constructed from an OK Status");
    }
  }
  StatusOr(T value)  // NOLINT(google-explicit-constructor)
      : value_(std::move(value)) {}

  bool ok() const { return status_.ok(); }
  const Status& status() const { return status_; }

  // Must only be called when ok() is true.
  T& value() & { return *value_; }
  const T& value() const& { return *value_; }
  T&& value() && { return *std::move(value_); }
  T& operator*() & { return *value_; }
  const T& operator*() const& { return *value_; }
  T* operator->() { return &*value_; }
  const T* operator->() const { return &*value_; }

 private:
  Status status_;
  std::optional<T> value_;
};

}  // namespace minicam

// Evaluates `expr` (a Status) and returns it from the enclosing function if it
// is not OK. A macro because a function cannot return from its caller; the
// same pattern as absl's RETURN_IF_ERROR.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MINICAM_RETURN_IF_ERROR(expr)                  \
  do {                                                 \
    ::minicam::Status minicam_status_ = (expr);        \
    if (!minicam_status_.ok()) return minicam_status_; \
  } while (false)

#endif  // MINICAM_STATUS_H_

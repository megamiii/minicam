#include "minicam/status.h"

#include <string>
#include <utility>

namespace minicam {

std::string_view StatusCodeName(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "OK";
    case StatusCode::kInvalidArgument:
      return "INVALID_ARGUMENT";
    case StatusCode::kNotFound:
      return "NOT_FOUND";
    case StatusCode::kFailedPrecondition:
      return "FAILED_PRECONDITION";
    case StatusCode::kOutOfRange:
      return "OUT_OF_RANGE";
    case StatusCode::kDataLoss:
      return "DATA_LOSS";
    case StatusCode::kCancelled:
      return "CANCELLED";
    case StatusCode::kInternal:
      return "INTERNAL";
  }
  return "UNKNOWN";
}

Status::Status(StatusCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

std::string Status::ToString() const {
  if (ok()) return "OK";
  std::string out(StatusCodeName(code_));
  out += ": ";
  out += message_;
  return out;
}

Status InvalidArgumentError(std::string message) {
  return {StatusCode::kInvalidArgument, std::move(message)};
}
Status NotFoundError(std::string message) {
  return {StatusCode::kNotFound, std::move(message)};
}
Status FailedPreconditionError(std::string message) {
  return {StatusCode::kFailedPrecondition, std::move(message)};
}
Status OutOfRangeError(std::string message) {
  return {StatusCode::kOutOfRange, std::move(message)};
}
Status DataLossError(std::string message) {
  return {StatusCode::kDataLoss, std::move(message)};
}
Status CancelledError(std::string message) {
  return {StatusCode::kCancelled, std::move(message)};
}
Status InternalError(std::string message) {
  return {StatusCode::kInternal, std::move(message)};
}

}  // namespace minicam

#include "minicam/status.h"

#include <string>

#include <gtest/gtest.h>

namespace minicam {
namespace {

TEST(StatusTest, DefaultIsOk) {
  Status status;
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status.code(), StatusCode::kOk);
  EXPECT_EQ(status.ToString(), "OK");
}

TEST(StatusTest, ErrorCarriesCodeAndMessage) {
  Status status = InvalidArgumentError("bad width");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "bad width");
  EXPECT_EQ(status.ToString(), "INVALID_ARGUMENT: bad width");
}

Status FailIfNegative(int value) {
  if (value < 0) return OutOfRangeError("negative");
  return OkStatus();
}

Status CallsFailIfNegative(int value, bool* reached_end) {
  MINICAM_RETURN_IF_ERROR(FailIfNegative(value));
  *reached_end = true;
  return OkStatus();
}

TEST(StatusTest, ReturnIfErrorStopsOnError) {
  bool reached_end = false;
  EXPECT_EQ(CallsFailIfNegative(-1, &reached_end).code(),
            StatusCode::kOutOfRange);
  EXPECT_FALSE(reached_end);

  EXPECT_TRUE(CallsFailIfNegative(1, &reached_end).ok());
  EXPECT_TRUE(reached_end);
}

TEST(StatusOrTest, HoldsValue) {
  StatusOr<std::string> result = std::string("hello");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, "hello");
  EXPECT_EQ(result->size(), 5u);
}

TEST(StatusOrTest, HoldsError) {
  StatusOr<int> result = NotFoundError("missing");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

TEST(StatusOrTest, OkStatusWithoutValueBecomesInternalError) {
  StatusOr<int> result = OkStatus();
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInternal);
}

}  // namespace
}  // namespace minicam

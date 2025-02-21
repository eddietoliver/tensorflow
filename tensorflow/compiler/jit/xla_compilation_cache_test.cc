/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/jit/xla_compilation_cache.h"

#include "tensorflow/compiler/jit/flags.h"
#include "tensorflow/compiler/tf2xla/shape_util.h"
#include "tensorflow/compiler/xla/client/client_library.h"
#include "tensorflow/core/platform/test.h"
#include "tensorflow/core/platform/test_benchmark.h"

namespace tensorflow {
namespace {

using SignatureHash = XlaCompilationCache::Signature::Hash;

TEST(XlaCompilationCacheTest, SignatureEquality) {
  NameAttrList fn;
  fn.set_name("afunction");
  std::vector<XlaCompiler::Argument> args(1);
  args[0].kind = XlaCompiler::Argument::kConstant;
  args[0].type = DT_INT32;
  args[0].shape = TensorShape({4, 0});
  args[0].constant_value = Tensor(DT_INT32, {4, 0});
  TF_ASSERT_OK_AND_ASSIGN(XlaCompilationCache::Signature s1,
                          XlaCompilationCache::BuildSignature(fn, args));

  args[0].type = DT_FLOAT;
  args[0].constant_value = Tensor(DT_FLOAT, {4, 0});
  TF_ASSERT_OK_AND_ASSIGN(XlaCompilationCache::Signature s2,
                          XlaCompilationCache::BuildSignature(fn, args));

  args[0].shape = TensorShape({0, 4});
  args[0].constant_value = Tensor(DT_FLOAT, {0, 4});
  TF_ASSERT_OK_AND_ASSIGN(XlaCompilationCache::Signature s3,
                          XlaCompilationCache::BuildSignature(fn, args));

  std::vector<XlaCompilationCache::Signature> signatures = {s1, s2, s3};
  for (int i = 0; i < signatures.size(); ++i) {
    for (int j = 0; j < signatures.size(); ++j) {
      EXPECT_EQ(i == j, signatures[i] == signatures[j])
          << "s1: " << signatures[i].HumanString() << "\n"
          << "s2: " << signatures[j].HumanString();
      EXPECT_EQ(i == j,
                signatures[i].HumanString() == signatures[j].HumanString())
          << "s1: " << signatures[i].HumanString() << "\n"
          << "s2: " << signatures[j].HumanString();
      EXPECT_EQ(i == j, SignatureHash()(signatures[i]) ==
                            SignatureHash()(signatures[j]))
          << "s1: " << signatures[i].HumanString() << "\n"
          << "s1_hash: " << SignatureHash()(signatures[i]) << "\n"
          << "s2: " << signatures[j].HumanString() << "\n"
          << "s2_hash: " << SignatureHash()(signatures[j]);
    }
  }
}

TEST(XlaCompilationCacheTest, SignatureUniqueness) {
  NameAttrList fn;
  fn.set_name("afunction");
  std::vector<XlaCompiler::Argument> args(2);
  args[0].kind = XlaCompiler::Argument::kConstant;
  args[0].type = DT_INT32;
  args[0].constant_value = Tensor(DT_INT32, {4, 0});

  args[1].kind = XlaCompiler::Argument::kParameter;
  args[1].type = DT_INT32;
  args[1].shape = TensorShape({4, 0});

  TF_ASSERT_OK_AND_ASSIGN(XlaCompilationCache::Signature s1,
                          XlaCompilationCache::BuildSignature(fn, args));

  using std::swap;  // go/using-std-swap
  swap(args[0], args[1]);
  TF_ASSERT_OK_AND_ASSIGN(XlaCompilationCache::Signature s2,
                          XlaCompilationCache::BuildSignature(fn, args));

  EXPECT_NE(s1.HumanString(), s2.HumanString());
  EXPECT_NE(SignatureHash()(s1), SignatureHash()(s2));
  EXPECT_FALSE(s1 == s2);
}

void BM_BuildSignature(::testing::benchmark::State& state) {
  const int n_args = state.range(0);

  NameAttrList fn;
  fn.set_name("afunction");
  for (int i = 0; i < n_args; i++) {
    (*fn.mutable_attr())[absl::StrCat("T", i)].set_type(DT_FLOAT);
  }
  std::vector<XlaCompiler::Argument> args(n_args);
  for (int i = 0; i < n_args; i++) {
    args[i].kind = (((i % 3) == 0) ? XlaCompiler::Argument::kConstant
                                   : XlaCompiler::Argument::kParameter);
    args[i].type = DT_INT32;
    args[i].shape = TensorShape({4, 0});
    args[i].constant_value = Tensor(DT_INT32, {4, 0});
  }

  for (auto i : state) {
    StatusOr<XlaCompilationCache::Signature> s =
        XlaCompilationCache::BuildSignature(fn, args);
    CHECK(s.ok());
    XlaCompilationCache::Signature sig = std::move(s.value());
  }
}
BENCHMARK(BM_BuildSignature)->Arg(0)->Arg(1)->Arg(2)->Arg(5)->Arg(10);

TEST(GetExecutableOptionTest, Basic) {
  XlaCompiler::Options options;
  options.device_ordinal = 0;
  options.alias_passthrough_params = true;
  options.detailed_logging = true;
  XlaCompiler::CompilationResult result;
  xla::Shape xla_output_shape;
  result.xla_output_shape = xla_output_shape;

  auto build_option =
      GetExecutableBuildOptions(options, result, /*default_device_ordinal=*/-1);

  EXPECT_EQ(build_option.device_ordinal(), 0);
  EXPECT_EQ(build_option.result_layout()->ToString(),
            xla_output_shape.ToString());
  EXPECT_EQ(build_option.alias_passthrough_params(), true);
  EXPECT_EQ(build_option.debug_options().xla_detailed_logging_and_dumping(),
            true);
  LOG(ERROR) << build_option.ToString();
}

TEST(GetExecutableOptionTest, DefaultDeviceOrdinal) {
  XlaCompiler::Options options;
  XlaCompiler::CompilationResult result;

  auto build_option =
      GetExecutableBuildOptions(options, result, /*default_device_ordinal=*/0);

  EXPECT_EQ(build_option.device_ordinal(), 0);
}

TEST(GetExecutableOptionTest, DeviceOrdinalNotSet) {
  XlaCompiler::Options options;
  XlaCompiler::CompilationResult result;

  auto build_option =
      GetExecutableBuildOptions(options, result, /*default_device_ordinal=*/-1);

  EXPECT_EQ(build_option.device_ordinal(), -1);
}

}  // namespace
}  // namespace tensorflow

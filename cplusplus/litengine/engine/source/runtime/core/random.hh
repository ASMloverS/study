// Copyright (c) 2025 ASMlover. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list ofconditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materialsprovided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <algorithm>
#include <cfloat>
#include <random>

namespace Litengine {

template <typename NumericType>
using UniformDistribution = typename std::conditional<std::is_integral<NumericType>::value,
                                                      std::uniform_int_distribution<NumericType>,
                                                      std::uniform_real_distribution<NumericType>>::type;

template <typename RandomEngine = std::default_random_engine>
class RandomNumberGenerator {
  RandomEngine engine_;
public:
  template <typename... Params> explicit RandomNumberGenerator(Params&&... params) noexcept
    : engine_(std::forward<Params>(params)...) {
  }

  template <typename... Params> void seed(Params&&... seeding) noexcept {
    engine_.seed(std::forward<Params>(seeding)...);
  }

  template <typename DistributionFunc, typename... Params>
  typename DistributionFunc::result_type distribution(Params&&... params) noexcept {
    DistributionFunc dist(std::forward<Params>(params)...);
    return dist(engine_);
  }

  template <typename NumericType> NumericType uniformDistribution(NumericType lower, NumericType upper) noexcept {
    if (lower == upper)
      return lower;
    return distribution<UniformDistribution<NumericType>>(lower, upper);
  }

  float uniformUnit() noexcept {
    return uniformDistribution(0.f, std::nextafter(1.f, FLT_MAX));
  }

  float uniformSymmetry() noexcept {
    return uniformDistribution(-1.f, std::nextafter(1.f, FLT_MAX));
  }

  bool bernoulliDistribution(float probability) noexcept {
    return distribution<std::bernoulli_distribution>(probability);
  }

  float normalDistribution(float mean, float stddev) noexcept {
    return distribution<std::normal_distribution<float>>(mean, stddev);
  }

  template <typename DistributionFunc, typename Range, typename... Params>
  void generator(Range&& range, Params&&... params) noexcept {
    DistributionFunc dist(std::forward<Params>(params)...);
    return std::generate(std::begin(range), std::end(range), [&] { return dist(engine_); });
  }
};

}

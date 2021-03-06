/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <algorithm>

#include <folly/detail/CacheLocality.h>

#include "mcrouter/config.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * A token bucket (http://en.wikipedia.org/wiki/Token_bucket). A token bucket
 * models a stream of events with an average rate and some amount of burstiness.
 * The canonical example is a packet switched network: the network can accept
 * some number of bytes per second and the bytes come in finite packets
 * (bursts). A token bucket stores up to a fixed number of tokens (the burst
 * size). Some number of tokens are removed when an event occurs. The tokens are
 * restored at a fixed rate.
 *
 * This implementation records the last time it was updated. This allows the
 * token bucket to add tokens "just in time" when tokens are requested.
 */
class AtomicTokenBucket {
 public:
  /**
   * Construct a token bucket with a specific maximum rate and burst size.
   *
   * @param rate Number of tokens to generate per second.
   * @param burstSize Maximum burst size. Must be at least 1.
   * @param nowInSeconds Current time in seconds according to some
   *                     monotonically increasing clock.
   *
   */
  AtomicTokenBucket(double rate, double burstSize,
                    double nowInSeconds = defaultClockNow())
      : zeroTime_(0),
        rate_(rate),
        burstSize_(burstSize) {
    assert(rate_ > 0);
    assert(burstSize_ >= 1);
  }

  /**
   * Attempts to consume some number of tokens. Tokens are first added to the
   * bucket based on the time elapsed since the last attempt to consume tokens.
   * Note: Attempts to consume more tokens than the burst size will always fail.
   *
   * @param toConsume The number of tokens to consume.
   * @param nowInSeconds Current time in seconds. Should be monotonically
   *                     increasing from the nowInSeconds specified in
   *                     this token bucket's constructor.
   * @return True if the rate limit check passed, false otherwise.
   */
  bool consume(double toConsume, double nowInSeconds = defaultClockNow()) {
    auto zeroTime = zeroTime_.load();
    double zeroTimeNew;
    do {
      auto tokens = std::min((nowInSeconds - zeroTime) * rate_, burstSize_);
      if (tokens < toConsume) {
        return false;
      }
      tokens -= toConsume;
      zeroTimeNew = nowInSeconds - tokens / rate_;
    } while (!zeroTime_.compare_exchange_weak(zeroTime, zeroTimeNew));

    return true;
  }

  /**
   * Returns the number of tokens currently available.
   */
  double available(double nowInSeconds = defaultClockNow()) const {
    return std::min((nowInSeconds - zeroTime_) * rate_, burstSize_);
  }

  static double defaultClockNow() {
    return nowSec();
  }

 private:
  // Stores the point in time when number of tokens in the bucket was zero, if
  // we assume that consume was never called after it. This is enough to know
  // the current state of the bucket (see available() implementation).
  std::atomic<double> zeroTime_ FOLLY_ALIGN_TO_AVOID_FALSE_SHARING;
  const double rate_;
  const double burstSize_;
};

}}} // namespace facebook::memcache::mcrouter

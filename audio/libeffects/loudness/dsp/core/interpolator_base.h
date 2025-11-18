/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LE_FX_ENGINE_DSP_CORE_INTERPOLATOR_BASE_H_
#define LE_FX_ENGINE_DSP_CORE_INTERPOLATOR_BASE_H_

#include <vector>
namespace le_fx {

namespace sigmod {

// Interpolation base-class that provides the interface, while it is the derived
// class that provides the specific interpolation algorithm. The following list
// of interpolation algorithms are currently present:
//
// InterpolationSine<T>: weighted interpolation between y_data[n] and
//                       y_data[n+1] using a sin(.) weighting factor from
//                       0 to pi/4.
// InterpolationLinear<T>: linear interpolation
// InterpolationSplines<T>: spline-based interpolation
//
// Example (using derived spline-based interpolation class):
//  InterpolatorSplines<float> interp(x_data, y_data, data_length);
//  for (int n = 0; n < data_length; n++) Y[n] = interp.Interpolate(X[n]);
//
template <typename T, class Algorithm>
class InterpolatorBase {
 public:
  InterpolatorBase() = default;
  InterpolatorBase(const InterpolatorBase&) = delete;
  InterpolatorBase& operator=(const InterpolatorBase&) = delete;

  ~InterpolatorBase() {
    delete [] state_;
    if (own_x_data_) {
        delete [] x_data_;
    }
  }
  // Generic random-access interpolation with arbitrary spaced x-axis samples.
  // Below X[0], the interpolator returns Y[0]. Above X[data_length-1], it
  // returns Y[data_length-1].
  T Interpolate(T x) {
      // Search for the containing interval
      if (x <= x_data_[cached_index_]) {
          if (cached_index_ <= 0) {
              cached_index_ = 0;
              return y_data_[0];
          }
          if (x >= x_data_[cached_index_ - 1]) {
              cached_index_--;  // Fast descending
          } else {
              if (x <= x_data_[0]) {
                  cached_index_ = 0;
                  return y_data_[0];
              }
              cached_index_ = SearchIndex(x_data_, x, 0, cached_index_);
          }
      } else {
          if (cached_index_ >= last_element_index_) {
              cached_index_ = last_element_index_;
              return y_data_[last_element_index_];
          }
          if (x > x_data_[cached_index_ + 1]) {
              if (cached_index_ + 2 > last_element_index_) {
                  cached_index_ = last_element_index_ - 1;
                  return y_data_[last_element_index_];
              }
              if (x <= x_data_[cached_index_ + 2]) {
                  cached_index_++;  // Fast ascending
              } else {
                  if (x >= x_data_[last_element_index_]) {
                      cached_index_ = last_element_index_ - 1;
                      return y_data_[last_element_index_];
                  }
                  cached_index_ = SearchIndex(
                          x_data_, x, cached_index_, last_element_index_);
              }
          }
      }
      // Compute interpolated value by calling the corresponding function of the
      // derived class.
      return static_cast<Algorithm*>(this)->MethodSpecificInterpolation(x);
  }

  bool get_status() const {
    return status_;
  }

  // Initializes internal buffers.
  //  x_data: [(data_length)x1] x-axis coordinates (searching axis)
  //  y_data: [(data_length)x1] y-axis coordinates (interpolation axis)
  //  data_length: number of points
  // returns `true` if everything is ok, `false`, otherwise
  bool Initialize(const T *x_data, const T *y_data, int data_length) {
    // Default settings
    cached_index_ = 0;
    data_length_ = 0;
    x_start_offset_ = 0;
    x_inverse_sampling_interval_ = 0;
    state_ = nullptr;
    // Input data is externally owned
    own_x_data_ = false;
    x_data_ = x_data;
    y_data_ = y_data;
    data_length_ = data_length;
    last_element_index_ = data_length - 1;
    // Check input data validity
    for (int n = 0; n < last_element_index_; ++n) {
        if (x_data_[n + 1] <= x_data_[n]) {
            ALOGE("InterpolatorBase::Initialize: xData are not ordered or "
                  "contain equal values (X[%d] <= X[%d]) (%.5e <= %.5e)",
                  n + 1, n, x_data_[n + 1], x_data_[n]);
            status_ = false;
            return false;
        }
    }
    // Pre-compute internal state by calling the corresponding function of the
    // derived class.
    status_ = static_cast<Algorithm*>(this)->SetInternalState();
    return status_;
}

  // Initializes internal buffers.
  //  x_data: x-axis coordinates (searching axis)
  //  y_data: y-axis coordinates (interpolating axis)
  // returns `true` if everything is ok, `false`, otherwise
  bool Initialize(const std::vector<T> &x_data, const std::vector<T> &y_data) {
    return Initialize(&x_data[0], &y_data[0], x_data.size());
}


    // Initialization for regularly sampled sequences, where:
  //  x_data[i] = x_start_offset + i * x_sampling_interval
  bool Initialize(double x_start_offset,
                  double x_sampling_interval,
                  const std::vector<T> &y_data) {
    return Initialize(x_start_offset,
            x_sampling_interval,
            &y_data[0],
            y_data.size());
}

  // Initialization for regularly sampled sequences, where:
  //  x_data[i] = x_start_offset + i * x_sampling_interval
  bool Initialize(double x_start_offset,
                  double x_sampling_interval,
                  const T *y_data,
                  int data_length) {
        // Constructs and populate x-axis data: `x_data_`
        T *x_data_tmp = new T[data_length];
        float time_offset = x_start_offset;
        for (int n = 0; n < data_length; n++) {
            x_data_tmp[n] = time_offset;
            time_offset += x_sampling_interval;
        }
        Initialize(x_data_tmp, y_data, data_length);
        // Sets-up the regularly sampled interpolation mode
        x_start_offset_ = x_start_offset;
        x_inverse_sampling_interval_ = 1.0 / x_sampling_interval;
        own_x_data_ = true;
        return status_;
    }

 protected:
  // Is set to false if something goes wrong, and to true if everything is ok.
  bool status_ = false;

  // The start-index of the previously searched interval
  int cached_index_ = 0;

// Searches for the interval that contains <x> using a divide-and-conquer
// algorithm.
// X[]: a std::vector of sorted values (X[i+1] > X[i])
// x:   a value
// StartIndex: the minimum searched index
// EndIndex: the maximum searched index
// returns: the index <i> that satisfies: X[i] <= x <= X[i+1] &&
//          StartIndex <= i <= (EndIndex-1)

    int SearchIndex(const T* x_data, T x, int start_index, int end_index) {
        auto x_begin = x_data + start_index;
        auto x_end = x_data + end_index - (end_index > start_index);
        auto iter = std::lower_bound(x_begin, x_end, x);
        return iter - x_data;
    }

  // Data points
  const T *x_data_ = nullptr;  // Externally or internally owned, depending on own_x_data_
  const T *y_data_ = nullptr;  // Externally owned (always)
  int data_length_ = 0;
  // Index of the last element `data_length_ - 1` kept here for optimization
  int last_element_index_= -1;
  bool own_x_data_ = false;
  // For regularly-samples sequences, keep only the boundaries and the intervals
  T x_start_offset_ = 0;
  float x_inverse_sampling_interval_ = 0;

  // Algorithm state (internally owned)
  double *state_ = nullptr;
};

}  // namespace sigmod

}  // namespace le_fx


#endif  // LE_FX_ENGINE_DSP_CORE_INTERPOLATOR_BASE_H_

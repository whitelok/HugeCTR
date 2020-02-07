/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HugeCTR/include/layers/multiply_layer.hpp"

#include "HugeCTR/include/data_parser.hpp"
#include "HugeCTR/include/general_buffer.hpp"
#include "gtest/gtest.h"
#include "utest/test_utils.h"

#include <vector>

using namespace std;
using namespace HugeCTR;

namespace {

const float eps = 1e-6;

template<typename T>
void multiply_cpu(const T * input, 
                 const T * weight, 
                 T * output, 
                 const int batch_size, 
                 const int vector_length) {
  for (int i = 0; i < (batch_size * vector_length); ++i) {
    output[i] = input[i] * weight[i % vector_length];
  }
}

template<typename T>
void multiply_wgrad_cpu(const T * top_grad,
                      const T * input,
                      T * wgrad,
                      const int batch_size,
                      const int vector_length) {
  for (int i = 0; i < (batch_size*vector_length); ++i) {
    wgrad[i] = input[i] * top_grad[i];
  }
}

template<typename T>
void multiply_dgrad_cpu(const T * top_grad,
                      const T * weight,
                      T * dgrad,
                      const int batch_size,
                      const int vector_length) {
  for (int i = 0; i < (batch_size * vector_length); ++i) {
    dgrad[i] = top_grad[i] * weight[i % vector_length];
  }
}

void multiply_test(int batch_size, int vector_length) {
  std::shared_ptr<GeneralBuffer<float>> buf(new GeneralBuffer<float>());
  vector<int> dims_in = {batch_size, vector_length};
  vector<int> dims_weight = {1, vector_length};

  std::shared_ptr<Tensor<float>> in_tensor(new Tensor<float>(dims_in, buf, TensorFormat_t::HW));
  std::shared_ptr<Tensor<float>> out_tensor(new Tensor<float>(dims_in, buf, TensorFormat_t::HW));
  std::shared_ptr<Tensor<float>> weight_tensor(new Tensor<float>(dims_weight, buf, TensorFormat_t::HW));
  std::shared_ptr<Tensor<float>> wgrad_tensor(new Tensor<float>(dims_in, buf, TensorFormat_t::HW));
  buf->init(0);

  const int len = batch_size * vector_length;
  float* d_in = in_tensor->get_ptr();
  float* d_out = out_tensor->get_ptr();
  float* d_weight = weight_tensor->get_ptr();
  float* d_wgrad = wgrad_tensor->get_ptr();
  std::unique_ptr<float[]> h_in(new float[len]);
  std::unique_ptr<float[]> h_out(new float[len]);
  std::unique_ptr<float[]> h_weight(new float[vector_length]);
  std::unique_ptr<float[]> h_wgrad(new float[len]);
  std::unique_ptr<float[]> h_expected(new float[len]);
  std::unique_ptr<float[]> h_expected_wgrad(new float[len]);

  GaussianDataSimulator<float> simulator(0.0, 1.0, -2.0, 2.0);
  MultiplyLayer multiply_layer(weight_tensor, wgrad_tensor, in_tensor, out_tensor, 0);

  // fprop
  for (int i = 0; i < len; ++i) {
    h_in[i] = simulator.get_num();
  }
  for(int i = 0; i < vector_length; ++i) {
    h_weight[i] = simulator.get_num();
  }
  cudaMemcpy(d_in, h_in.get(), len * sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(d_weight, h_weight.get(), vector_length * sizeof(float), cudaMemcpyHostToDevice);
  multiply_layer.fprop(cudaStreamDefault);
  cudaMemcpy(h_out.get(), d_out, len * sizeof(float), cudaMemcpyDeviceToHost);

  multiply_cpu(h_in.get(), h_weight.get(), h_expected.get(), batch_size, vector_length);
  ASSERT_TRUE(test::compare_array_approx<float>(h_out.get(), h_expected.get(), len, eps));

  // bprop
  for (int i = 0; i < len; ++i) {
    h_in[i] = simulator.get_num();
    h_out[i] = simulator.get_num(); // top_grad
    h_expected[i] = h_in[i];
  }
  for(int i = 0; i < vector_length; ++i) {
    h_weight[i] = simulator.get_num();
  }
  cudaMemcpy(d_in, h_in.get(), len * sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(d_out, h_out.get(), len * sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(d_weight, h_weight.get(), vector_length * sizeof(float), cudaMemcpyHostToDevice);
  multiply_layer.bprop(cudaStreamDefault); // compute wgrad and dgrad
  cudaMemcpy(h_wgrad.get(), d_wgrad, len * sizeof(float), cudaMemcpyDeviceToHost); // wgrad
  cudaMemcpy(h_in.get(), d_in, len * sizeof(float), cudaMemcpyDeviceToHost); // dgrad

  multiply_wgrad_cpu(h_out.get(), h_expected.get(), h_expected_wgrad.get(), batch_size, vector_length);
  ASSERT_TRUE(test::compare_array_approx<float>(h_wgrad.get(), h_expected_wgrad.get(), len, eps)); // compare wgrad

  multiply_dgrad_cpu(h_out.get(), h_weight.get(), h_expected.get(), batch_size, vector_length);
  ASSERT_TRUE(test::compare_array_approx<float>(h_in.get(), h_expected.get(), len, eps)); // compare dgrad
}

}  // namespace

TEST(multiply_layer, fprop_and_bprop) {
  multiply_test(4, 32);
  multiply_test(4, 128);
  multiply_test(40960, 128);
}

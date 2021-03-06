// Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <NvInfer.h>
#include <mutex>
#include "src/core/infer.h"
#include "src/core/label_provider.h"
#include "src/core/model_config.pb.h"
#include "tensorflow/core/lib/core/errors.h"

namespace nvidia { namespace inferenceserver {

class PlanBundle : public InferenceServable {
 public:
  PlanBundle() = default;
  PlanBundle(PlanBundle&&) = default;

  tensorflow::Status Init(
    const tensorflow::StringPiece& path, const ModelConfig& config);

  // Create a context for execution for each instance for the
  // serialized plans specified in 'models'.
  tensorflow::Status CreateExecutionContexts(
    const std::unordered_map<std::string, std::vector<char>>& models);
  tensorflow::Status CreateExecutionContext(
    const std::string& instance_name, const int gpu_device,
    const std::unordered_map<std::string, std::vector<char>>& models);

  tensorflow::Status GetOutputDataType(
    const std::string& name, DataType* dtype) const override;
  const LabelProvider& GetLabelProvider() const override
  {
    return label_provider_;
  }

 protected:
  void Run(
    uint32_t runner_idx, std::vector<RunnerPayload>* payloads,
    std::function<void(tensorflow::Status)> OnCompleteQueuedPayloads) override;

 private:
  TF_DISALLOW_COPY_AND_ASSIGN(PlanBundle);
  friend std::ostream& operator<<(std::ostream&, const PlanBundle&);

  // Label provider for this bundle.
  LabelProvider label_provider_;

  // Map from an output name to the datatype of that output.
  std::unordered_map<std::string, DataType> output_dtype_map_;

  // For each model instance there is a context.
  struct Context {
    // GPU device number that indicates that no gpu is available for a
    // context (which is an invalid state since TensorRT requires a
    // GPU).
    static constexpr int NO_GPU_DEVICE = -1;

    // Max batch size value that indicates batching is not supported.
    static constexpr int NO_BATCHING = 0;

    Context(
      const std::string& name, const int gpu_device, const int max_batch_size);
    Context(Context&& o);
    ~Context();

    TF_DISALLOW_COPY_AND_ASSIGN(Context);

    tensorflow::Status InitializeInputBindings(
      const ::google::protobuf::RepeatedPtrField<ModelInput>& ios);
    tensorflow::Status InitializeOutputBindings(
      const ::google::protobuf::RepeatedPtrField<ModelOutput>& ios);

    // Run model to execute for one or more requests. This function
    // assumes that it is only called by the single runner thread that
    // is assigned to this context. A non-OK return status indicates
    // an internal error that prevents any of the of requests from
    // completing. If an error is isolate to a single request payload
    // it will be reported in that payload.
    tensorflow::Status Run(std::vector<RunnerPayload>* payloads);

    // Name of the model instance
    const std::string name_;

    // The GPU index active when this context was created.
    const int gpu_device_;

    // Maximum batch size to allow. This is the minimum of what is
    // supported by the model and what is requested in the
    // configuration.
    const int max_batch_size_;

    // TensorRT components for the model
    nvinfer1::IRuntime* runtime_;
    nvinfer1::ICudaEngine* engine_;
    nvinfer1::IExecutionContext* context_;

    // The number of inputs required for this model.
    size_t num_inputs_;

    // For each binding index of the TensorRT engine, the size of the
    // corresponding tensor and pointer to the CUDA buffer for the
    // tensor. These are arrays with size equal to number of bindings.
    uint64_t* byte_sizes_;
    void** buffers_;

    // The stream where operations are executed.
    cudaStream_t stream_;
  };

  std::vector<Context> contexts_;
};

std::ostream& operator<<(std::ostream& out, const PlanBundle& pb);

}}  // namespace nvidia::inferenceserver

#include "ziliu/broker/onnx_scorer.h"

#include "scorer_assets.h"
#include "scorer_platform.h"

#include <onnxruntime_c_api.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ziliu::broker {
namespace {

using core::scoring::PackedInputs;
static_assert(ORT_API_VERSION == 20, "Use the verified ONNX Runtime 1.20.1 C API header");
constexpr std::array<const char*, 5> kInputs{
    "ids", "parent_flat", "targets", "score_indices", "score_valid"};

void Require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}

class Engine final {
 public:
  explicit Engine(const detail::VerifiedScorerAssets& assets)
      : runtime_(assets.runtime_path, assets.runtime_sha256) {
    try {
      using GetApi = const OrtApiBase*(ORT_API_CALL*)();
      const auto get_api = reinterpret_cast<GetApi>(runtime_.Symbol("OrtGetApiBase"));
      const auto* base = get_api();
      Require(base != nullptr && std::string(base->GetVersionString()) == "1.20.1",
              "unsupported ONNX Runtime version");
      api_ = base->GetApi(ORT_API_VERSION);
      Require(api_ != nullptr, "ONNX Runtime C API unavailable");
      // Fatal-only runtime logging; model/runtime diagnostics can contain input
      // metadata or paths and must not become ordinary Broker logs.
      Check(api_->CreateEnv(ORT_LOGGING_LEVEL_FATAL, "ziliu-scorer", &environment_));
      Check(api_->CreateSessionOptions(&options_));
      Check(api_->SetIntraOpNumThreads(options_, 2));
      Check(api_->SetInterOpNumThreads(options_, 1));
      Check(api_->SetSessionExecutionMode(options_, ORT_SEQUENTIAL));
      Check(api_->SetSessionGraphOptimizationLevel(options_, ORT_ENABLE_ALL));
      Check(api_->AddSessionConfigEntry(options_, "session.intra_op.allow_spinning", "0"));
      Check(api_->AddSessionConfigEntry(options_, "session.inter_op.allow_spinning", "0"));
      using AddCpu = OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*, int);
      const auto add_cpu = reinterpret_cast<AddCpu>(
          runtime_.Symbol("OrtSessionOptionsAppendExecutionProvider_CPU"));
      Check(add_cpu(options_, 1));
      Check(api_->CreateSessionFromArray(environment_, assets.graph.data(), assets.graph.size(),
                                        options_, &session_));
      Check(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_));
      ValidateInterface();
      detail::CheckWorkerResources();
    } catch (...) {
      Cleanup();
      throw;
    }
  }

  ~Engine() { Cleanup(); }
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  std::vector<float> Run(const PackedInputs& packed) {
    std::array<OrtValue*, 5> inputs{};
    OrtValue* output = nullptr;
    OrtTensorTypeAndShapeInfo* info = nullptr;
    const auto cleanup = [&] {
      for (auto* value : inputs) {
        if (value != nullptr) api_->ReleaseValue(value);
      }
      if (info != nullptr) api_->ReleaseTensorTypeAndShapeInfo(info);
      if (output != nullptr) api_->ReleaseValue(output);
    };
    try {
      const std::int64_t batch_sequence[]{packed.batch, packed.sequence};
      const std::int64_t target_count[]{packed.target_count};
      const std::int64_t batch_width[]{packed.batch, 63};
      // ORT's borrowed-input API accepts mutable storage. Copy so no ORT call can
      // mutate the host's frozen admission/identity snapshot.
      auto data = packed;
      std::array<void*, 5> buffers{data.ids.data(), data.parents.data(), data.targets.data(),
                                   data.score_indices.data(), data.score_valid.data()};
      const std::array<std::size_t, 5> sizes{
          data.ids.size() * sizeof(std::int64_t), data.parents.size() * sizeof(std::int64_t),
          data.targets.size() * sizeof(std::int64_t), data.score_indices.size() * sizeof(std::int64_t),
          data.score_valid.size()};
      for (std::size_t i = 0; i < inputs.size(); ++i) {
        const bool vector = i == 1 || i == 2;
        const auto* shape = i == 0 ? batch_sequence : vector ? target_count : batch_width;
        const auto type = i == 4 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL
                                 : ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
        Check(api_->CreateTensorWithDataAsOrtValue(memory_, buffers[i], sizes[i], shape,
                                                   vector ? 1 : 2, type, &inputs[i]));
      }
      const char* output_name = "scores";
      Check(api_->Run(session_, nullptr, kInputs.data(), inputs.data(), inputs.size(), &output_name, 1,
                      &output));
      Require(data == packed, "runtime mutated input tensors");
      int is_tensor = 0;
      Check(api_->IsTensor(output, &is_tensor));
      Require(is_tensor == 1, "invalid score tensor");
      Check(api_->GetTensorTypeAndShape(output, &info));
      ONNXTensorElementDataType type;
      Check(api_->GetTensorElementType(info, &type));
      std::size_t rank = 0;
      Check(api_->GetDimensionsCount(info, &rank));
      Require(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && rank == 1, "invalid score dtype or rank");
      std::int64_t dimension = 0;
      std::size_t count = 0;
      Check(api_->GetDimensions(info, &dimension, 1));
      Check(api_->GetTensorShapeElementCount(info, &count));
      Require(dimension == packed.batch && count == static_cast<std::size_t>(packed.batch),
              "incomplete score output");
      void* raw = nullptr;
      Check(api_->GetTensorMutableData(output, &raw));
      Require(raw != nullptr, "missing score output");
      const auto* floats = static_cast<const float*>(raw);
      std::vector<float> result(floats, floats + count);
      for (const auto score : result) Require(std::isfinite(score), "nonfinite score output");
      cleanup();
      return result;
    } catch (...) {
      cleanup();
      throw;
    }
  }

 private:
  void Check(OrtStatus* status) const {
    if (status != nullptr) {
      api_->ReleaseStatus(status);
      throw std::runtime_error("ONNX Runtime operation failed");
    }
  }

  void ValidateInterface() const {
    std::size_t count = 0;
    Check(api_->SessionGetInputCount(session_, &count));
    Require(count == 5, "invalid graph input count");
    Check(api_->SessionGetOutputCount(session_, &count));
    Require(count == 1, "invalid graph output count");
    OrtAllocator* allocator = nullptr;
    Check(api_->GetAllocatorWithDefaultOptions(&allocator));
    for (std::size_t index = 0; index < 6; ++index) {
      const bool output = index == 5;
      char* name = nullptr;
      OrtTypeInfo* type_info = nullptr;
      const auto cleanup = [&] {
        if (name != nullptr) allocator->Free(allocator, name);
        if (type_info != nullptr) api_->ReleaseTypeInfo(type_info);
      };
      try {
        Check(output ? api_->SessionGetOutputName(session_, 0, allocator, &name)
                     : api_->SessionGetInputName(session_, index, allocator, &name));
        Require(name != nullptr && std::string(name) == (output ? "scores" : kInputs[index]),
                "invalid graph tensor name");
        Check(output ? api_->SessionGetOutputTypeInfo(session_, 0, &type_info)
                     : api_->SessionGetInputTypeInfo(session_, index, &type_info));
        const OrtTensorTypeAndShapeInfo* tensor_info = nullptr;
        Check(api_->CastTypeInfoToTensorInfo(type_info, &tensor_info));
        Require(tensor_info != nullptr, "non-tensor graph interface");
        ONNXTensorElementDataType type;
        Check(api_->GetTensorElementType(tensor_info, &type));
        Require(type == (output ? ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
                                : index == 4 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL
                                             : ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64),
                "invalid graph tensor dtype");
        std::size_t rank = 0;
        Check(api_->GetDimensionsCount(tensor_info, &rank));
        const bool vector = output || index == 1 || index == 2;
        Require(rank == (vector ? 1u : 2u), "invalid graph tensor rank");
        std::array<std::int64_t, 2> shape{};
        Check(api_->GetDimensions(tensor_info, shape.data(), rank));
        Require(shape[0] == -1, "graph batch/target dimension must be dynamic");
        if (!vector) Require(shape[1] == (index == 0 ? -1 : 63), "invalid graph sequence width");
        cleanup();
      } catch (...) {
        cleanup();
        throw;
      }
    }
  }

  void Cleanup() noexcept {
    if (api_ == nullptr) return;
    if (memory_ != nullptr) api_->ReleaseMemoryInfo(memory_);
    if (session_ != nullptr) api_->ReleaseSession(session_);
    if (options_ != nullptr) api_->ReleaseSessionOptions(options_);
    if (environment_ != nullptr) api_->ReleaseEnv(environment_);
    memory_ = nullptr;
    session_ = nullptr;
    options_ = nullptr;
    environment_ = nullptr;
  }

  detail::RuntimeLibrary runtime_;
  const OrtApi* api_ = nullptr;
  OrtEnv* environment_ = nullptr;
  OrtSessionOptions* options_ = nullptr;
  OrtSession* session_ = nullptr;
  OrtMemoryInfo* memory_ = nullptr;
};

}  // namespace

std::unique_ptr<core::scoring::Session> CreateOnnxScorer(
    const TrustedScorerAssets& trusted, core::scoring::Scope scope, std::uint64_t maximum_calls,
    double lifetime_seconds) {
  Require(scope.session != 0 && scope.generation != 0 && maximum_calls > 0 &&
              maximum_calls <= 100000 && std::isfinite(lifetime_seconds) &&
              lifetime_seconds > 0 && lifetime_seconds <= 300,
          "invalid scorer factory bounds");
  detail::CheckWorkerResources();
  auto assets = detail::LoadScorerAssets(trusted);
  auto engine = std::make_shared<Engine>(assets);
  return std::make_unique<core::scoring::Session>(
      scope, std::move(assets.vocabulary), std::move(assets.manifest_sha256),
      [engine](const PackedInputs& inputs) { return engine->Run(inputs); },
      detail::CheckWorkerResources, maximum_calls, lifetime_seconds);
}

}  // namespace ziliu::broker

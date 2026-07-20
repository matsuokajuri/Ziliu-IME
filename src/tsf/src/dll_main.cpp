#include "ziliu/tsf/guids.h"
#include "ziliu/tsf/module_state.h"
#include "ziliu/tsf/text_service.h"

#include <unknwn.h>
#include <windows.h>

#include <atomic>
#include <new>

namespace ziliu::tsf {

HINSTANCE g_module_instance = nullptr;
std::atomic<long> g_module_references = 0;

class ClassFactory final : public IClassFactory {
 public:
  ClassFactory() { AddModuleReference(); }
  ClassFactory(const ClassFactory&) = delete;
  ClassFactory& operator=(const ClassFactory&) = delete;

  STDMETHODIMP QueryInterface(REFIID interface_id, void** object) override {
    if (object == nullptr) {
      return E_INVALIDARG;
    }
    *object = nullptr;
    if (IsEqualIID(interface_id, IID_IUnknown) || IsEqualIID(interface_id, IID_IClassFactory)) {
      *object = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override { return ++reference_count_; }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = --reference_count_;
    if (count == 0) {
      delete this;
    }
    return count;
  }

  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID interface_id, void** object) override {
    if (object == nullptr) {
      return E_INVALIDARG;
    }
    *object = nullptr;
    if (outer != nullptr) {
      return CLASS_E_NOAGGREGATION;
    }

    auto* service = new (std::nothrow) TextService();
    if (service == nullptr) {
      return E_OUTOFMEMORY;
    }
    const HRESULT result = service->QueryInterface(interface_id, object);
    service->Release();
    return result;
  }

  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock) {
      AddModuleReference();
    } else {
      ReleaseModuleReference();
    }
    return S_OK;
  }

 private:
  ~ClassFactory() { ReleaseModuleReference(); }
  std::atomic<ULONG> reference_count_{1};
};

void AddModuleReference() noexcept { ++g_module_references; }
void ReleaseModuleReference() noexcept { --g_module_references; }
long ModuleReferenceCount() noexcept { return g_module_references.load(); }
HINSTANCE ModuleInstance() noexcept { return g_module_instance; }

}  // namespace ziliu::tsf

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void* reserved) {
  static_cast<void>(reserved);
  if (reason == DLL_PROCESS_ATTACH) {
    ziliu::tsf::g_module_instance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
  return ziliu::tsf::ModuleReferenceCount() == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID class_id, REFIID interface_id,
                                               void** object) {
  if (object == nullptr) {
    return E_INVALIDARG;
  }
  *object = nullptr;
  if (!IsEqualCLSID(class_id, ziliu::tsf::kTextServiceClsid)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  auto* factory = new (std::nothrow) ziliu::tsf::ClassFactory();
  if (factory == nullptr) {
    return E_OUTOFMEMORY;
  }
  const HRESULT result = factory->QueryInterface(interface_id, object);
  factory->Release();
  return result;
}

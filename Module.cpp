#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <iostream>

#include "dawn/dawn_proc.h"
#include "dawn/webgpu_cpp.h"
#include "dawn/native/DawnNative.h"
#include "src/dawn/node/interop/Napi.h"
#include "src/dawn/node/interop/WebGPU.h"
#include "src/dawn/node/binding/Flags.h"
#include "src/dawn/node/binding/GPUAdapter.h"
#include "src/dawn/node/binding/GPUDevice.h"
#include "src/dawn/node/binding/GPUTextureView.h"
#include "src/dawn/node/interop/Core.h"
#include "third_party/node-addon-api/napi.h"
#include "GLFW/glfw3.h"
#include "webgpu/webgpu_glfw.h"



#if defined(_WIN32)
#include <Windows.h>
#endif

using namespace std;
using namespace wgpu;
using namespace Napi;

#if defined(__APPLE__)
constexpr auto defaultBackendType = BackendType::Metal;
#else
constexpr auto defaultBackendType = BackendType::Vulkan;
#endif

const DawnProcTable *procs;

class WinCtx : public Napi::ObjectWrap<WinCtx> {

public:
    int width;
    int height;
    string title;
    GLFWwindow* window;
    wgpu::SwapChain swapChain;
    WGPUSurface surface;
    FunctionReference callback;
    const Napi::Env *callbackEnv;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "WinCtx", {
                InstanceMethod("getCurrentTexture", &WinCtx::getCurrentTexture),
                InstanceMethod("createView", &WinCtx::createView),
                InstanceMethod("refresh", &WinCtx::refresh),
                InstanceMethod("configure", &WinCtx::configure),
                InstanceMethod("close", &WinCtx::close),
            });
        FunctionReference* constructor = new FunctionReference();
        *constructor = Persistent(func);
        env.SetInstanceData(constructor);
        return exports;
    }

    WinCtx::WinCtx(const Napi::CallbackInfo& info) : Napi::ObjectWrap<WinCtx>(info) {};

    static Napi::Object WinCtx::NewInstance(Napi::Env env) {
        Napi::Object obj = env.GetInstanceData<Napi::FunctionReference>()->New({});
        return obj;
    }
    
    Napi::Value createView(const CallbackInfo& info) {
        const auto& env = info.Env();
        return interop::GPUTextureView::Create<binding::GPUTextureView>(env, swapChain.GetCurrentTextureView());
    }

    Napi::Value getCurrentTexture(const CallbackInfo& info) {
        const auto& env = info.Env();
        return info.This();
    }

    Napi::Value configure(const CallbackInfo& info) {
        const auto& env = info.Env();
        if (info.Length() != 1 || !info[0].IsObject()) {
            Error::New(env, "invalid arguments").ThrowAsJavaScriptException();
            return env.Null();
        }
        auto args = info[0].As<Napi::Object>();
        auto jsDevice = args.Get("device");
        if (!jsDevice.IsObject()) {
            Error::New(env, "device not object").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        auto device = (binding::GPUDevice*) binding::GPUDevice::Unwrap(jsDevice.As<Object>());
        if (device == nullptr) {
            Error::New(env, "unwrap returned nullptr").ThrowAsJavaScriptException();
            return env.Undefined();   
        }        
        wgpu::SwapChainDescriptor swapChainDesc;
        swapChainDesc.usage = wgpu::TextureUsage::RenderAttachment;
        swapChainDesc.format = wgpu::TextureFormat::BGRA8Unorm;
        swapChainDesc.width = width;
        swapChainDesc.height = height;
        swapChainDesc.presentMode = wgpu::PresentMode::Mailbox;
        swapChain = device->device_.CreateSwapChain(surface, &swapChainDesc);
        return env.Undefined();
    }
    
    Napi::Value close(const CallbackInfo& info) {
        const auto& env = info.Env();
        glfwDestroyWindow(window);
        glfwSetCursorPosCallback(window, nullptr);
        glfwSetMouseButtonCallback(window, nullptr);
        glfwSetScrollCallback(window, nullptr);
        glfwSetKeyCallback(window, nullptr);
        glfwSetCursorEnterCallback(window, nullptr);
        glfwTerminate();
        return env.Undefined();
    }

    Napi::Value refresh(const CallbackInfo& info) {
        const auto& env = info.Env();
        swapChain.Present();
        callbackEnv = &env;
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            event("quit");
        return env.Undefined();
        
    }

    template<typename... Args>
    void event(string type, Args... args) {
        vector<double> argVec;
        (argVec.push_back(args), ...);
        vector<napi_value> jsArgs = { Napi::String::New(*callbackEnv, type) };
        for (double arg : argVec)
            jsArgs.push_back(Napi::Number::New(*callbackEnv, arg));
        callback.Call(jsArgs);
    }
    
    template<typename... Args>
    static void route(GLFWwindow *window, std::string type, Args... args) {
        WinCtx *ctx = (WinCtx*) glfwGetWindowUserPointer(window);
        ctx->event(type, args...);
    }

    static void PrintGLFWError(int code, const char* message) {
        cerr << "GLFW error: " << code << " - " << message << endl;
    }

};


class NodeGPU : public Napi::ObjectWrap<NodeGPU> {

private:
    binding::Flags flags_;
    dawn::native::Instance instance_;      

public:

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Function func = DefineClass(env, "GPU", {      
                InstanceMethod("requestAdapter", &NodeGPU::requestAdapter),
                InstanceMethod("getPreferredCanvasFormat", &NodeGPU::getPreferredCanvasFormat),
                InstanceMethod("createWindow", &NodeGPU::createWindow) 
            });
        FunctionReference* constructor = new FunctionReference();
        *constructor = Persistent(func);
        env.SetInstanceData(constructor);
        exports.Set("GPU", func);
        return exports;
    }
    
    NodeGPU(const CallbackInfo& info) : ObjectWrap<NodeGPU>(info) {
        const auto& env = info.Env();    
        tuple<vector<string>> args;
        auto res = interop::FromJS(info, args);
        if (res != interop::Success) {
            Error::New(env, res.error).ThrowAsJavaScriptException();
            return;
        }
        for (const auto& arg : get<0>(args)) {
            const size_t sep_index = arg.find("=");
            if (sep_index == string::npos) {
                Error::New(env, "flags should be key=value").ThrowAsJavaScriptException();
                return;
            }
            flags_.Set(arg.substr(0, sep_index), arg.substr(sep_index + 1));
        }

        instance_.EnableBackendValidation(true);
        instance_.SetBackendValidationLevel(dawn::native::BackendValidationLevel::Full);        
#if defined(_WIN32)
        if (auto dir = flags_.Get("dlldir"))
           ::SetDllDirectory(dir->c_str());
#endif
        instance_.DiscoverDefaultAdapters();
    }
    
    Napi::Value requestAdapter(const CallbackInfo& info) {
        const auto& env = info.Env();        
        auto promise = interop::Promise<optional<interop::Interface<interop::GPUAdapter>>>(env, PROMISE_INFO);        
        auto backendType = defaultBackendType;
        if (auto f = flags_.Get("dawn-backend")) {
            if (*f == "d3d12" || *f == "d3d")
                backendType = BackendType::D3D12;
            else if (*f == "metal")
                backendType = BackendType::Metal;
            else if (*f == "vulkan" || *f == "vk")
                backendType = BackendType::Vulkan;
        }

        auto adapters = instance_.GetAdapters();
        AdapterProperties props;
        int best = -1, bestScore = 0;
        for (int i = 0; i < adapters.size(); i++) {
            adapters[i].GetProperties(&props);
            int score = (props.backendType == backendType ? 1 : 0) * (props.adapterType == AdapterType::DiscreteGPU);
            if (score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        for (int i = 0; i < adapters.size(); i++) {
            adapters[i].GetProperties(&props);            
            cerr << (best == i ? "* " : "  ") << props.name << " : " << props.driverDescription << " (" << props.adapterType << ", " << props.backendType << ")\n";
        }                
        if (best == -1) {
            promise.Reject("no adapter found for backend");
            return interop::ToJS(env, promise);
        }
        
        auto adapter = binding::GPUAdapter::Create<binding::GPUAdapter>(env, adapters[best], flags_);
        promise.Resolve(optional<interop::Interface<interop::GPUAdapter>>(adapter));
        return interop::ToJS(env, promise);
    }
    
    Napi::Value getPreferredCanvasFormat(const Napi::CallbackInfo& info) {
    const auto& env = info.Env();
        return String::New(env, "bgra8unorm");
    }

    Napi::Value createWindow(const Napi::CallbackInfo& info) {
        const auto& env = info.Env();
        if (info.Length() != 4 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsString() || !info[3].IsFunction()) {
            Error::New(env, "invalid arguments").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        
        glfwSetErrorCallback(WinCtx::PrintGLFWError);
        if (!glfwInit()) {
            Error::New(env, "could not init glfw").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        Napi::Object jsCtx = WinCtx::NewInstance(info.Env());
        WinCtx *ctx = ObjectWrap<WinCtx>::Unwrap(jsCtx);

        ctx->width = info[0].As<Number>();
        ctx->height = info[1].As<Number>();
        ctx->title = info[2].As<String>();
        ctx->callback = Napi::Persistent(info[3].As<Function>());
        
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);        
        ctx->window = glfwCreateWindow(ctx->width, ctx->height, ctx->title.c_str(), nullptr, nullptr);
        if (!ctx->window) {
            glfwTerminate();
            Error::New(env, "could not create window").ThrowAsJavaScriptException();
            return env.Undefined();    
        }
        glfwSetWindowUserPointer(ctx->window, ctx);
        glfwSetCursorPosCallback(ctx->window, [](GLFWwindow* win, double a, double b) { WinCtx::route(win,"cursorPos",a,b); });
        glfwSetMouseButtonCallback(ctx->window, [](GLFWwindow* win, int a, int b, int c) { WinCtx::route(win,"mouseButton",a,b,c); });
        glfwSetScrollCallback(ctx->window, [](GLFWwindow* win, double a, double b) { WinCtx::route(win,"scroll",a,b); });
        glfwSetKeyCallback(ctx->window, [](GLFWwindow* win, int a, int b, int c, int d) { WinCtx::route(win,"key",a,b,c,d); });
        glfwSetCursorEnterCallback(ctx->window, [](GLFWwindow* win, int a) { WinCtx::route(win,"cursorEnter",a); });

        auto surfaceChainedDesc = wgpu::glfw::SetupWindowAndGetSurfaceDescriptor(ctx->window);
        WGPUSurfaceDescriptor surfaceDesc;
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(surfaceChainedDesc.get());
        ctx->surface = procs->instanceCreateSurface(instance_.Get(), &surfaceDesc);        

        return jsCtx;
    }

};

Napi::Object Initialize(Env env, Napi::Object exports) {
    procs = &dawn::native::GetProcs();
    dawnProcSetProcs(procs);
    wgpu::interop::Initialize(env);
    NodeGPU::Init(env,exports);
    WinCtx::Init(env,exports);
    return exports;
}

NODE_API_MODULE(addon, Initialize)
    

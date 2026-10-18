# AI 模块复用

## 依赖与边界

`src/translate/CMakeLists.txt` 提供静态库 `SnapLens::Ai`，要求 C++17 和 Qt 6.3+ 的 Core、Network 模块。网络层不依赖 Widgets、OCR、应用设置或 SnapLens 日志库，日志使用 Qt 的 `ai.client` category。

| 模块 | 职责 |
| --- | --- |
| ai_client | Chat Completions 请求构建、JSON/SSE 响应、模型列表；显式参数优先，不重试改写参数 |
| reply_waiter | 连接/发送、响应空闲、总时限与跨线程原子取消；不读取或拥有响应 |
| model_capabilities | 从原始模型条目提取明确能力；带来源的展示候选与请求参数分开 |
| providers | 接口地址、字段形式预设及端点/模型身份；不代表整家服务的模型能力 |
| ai_request_options | **应用桥接层**：读取 Settings，复制模型配置，并将两种翻译任务转换成同一种客户端调用 |
| ai_model_selector | **应用 UI 层**：文本/截图翻译共用的快捷选择；只生成请求快照，不持久化设置 |
| translator | **应用任务层**：OCR、提示词与 QThread 信号；网络请求委托给快照的统一执行入口 |

前三个模块和 providers.h 可以随目标单独引入其他 Qt 项目。其余模块可根据目标项目的设置结构、UI 库、任务调度方式替换。

## CMake 接入

在消费方工程中：

```cmake
find_package(Qt6 6.3 REQUIRED COMPONENTS Core Network)
add_subdirectory(path/to/SnapLens/src/translate snaplens-ai)
target_link_libraries(my_app PRIVATE SnapLens::Ai)
```

使用静态 Qt/MSVC 时，消费方需要为所有目标选择一致的 MSVC runtime。SnapLens 主工程会为此库设置相同的 runtime；其他工程可使用 CMAKE_MSVC_RUNTIME_LIBRARY 统一配置。

## 离线构建请求与在线执行

```cpp
#include <ai_client.h>

ApiClient::ChatParams request;
request.apiBase = "https://example.com/v1";
request.model = "your-model-id";
request.provider = "custom";
request.messages = QJsonArray{
    QJsonObject{{"role", "user"}, {"content", "待处理文本"}}
};
request.timeoutSecs = 30;             // 连接与发送阶段
request.responseIdleTimeoutSecs = 0; // 不限制连续静默等待
// 从安全配置中赋值 apiKey，不将密钥写进日志或请求正文。
// enabledParameters 默认空，temperature 等细致参数不发送。

QString error;
const auto body = ApiClient::buildRequestBody(request, true, &error);
// 到这里没有网络请求；可用于预览与校验。
```

确认需要在线调用时，在有 Qt 事件分发器的工作线程中创建 ApiClient，再调用 chatCreate 或 chatCreateStream。两者会等待请求结束并处理该线程的事件，不应在 GUI 线程等待生成。模型发现的 listModelsAsync 可直接用于 GUI 线程，回调在客户端所属线程执行。客户端与 QNetworkAccessManager 不得跨线程共享。

chatCreateStream 的回调接收累计正文、累计思考文本、final、错误码与错误信息；每次请求恰好一次 final，调用方不要再依据返回码重复通知。传入 cancelFlag 时，其生命周期必须覆盖整个调用；取消或超时会 abort 当前网络响应。停止本地连接不保证供应商立即停止已接收的服务端任务。

## 资料与请求配置

模型列表接口只在明确的 GET 路径上发现资料，不通过试发会话推测能力。parseModels 保留原始条目，ModelCapabilities::fromMetadata 解析明确声明；未声明与空列表区分。suggestions 才会加入带来源的文档候选，它只用于展示，不用于启用参数或自动填写输出预算。

调用方负责向 ChatParams.modelMetadata 传入当前端点/模型的原始条目。参数默认值由上游决定；只有显式配置的字段才进入请求。extraBody 的顶层 null 是本地删除标记，model/messages/stream 受保护，详见 [AI 参数说明](ai-parameters.md)。

新增协议时应实现相应的请求、响应与发现接口。目前此库使用 Chat Completions 格式；原生 Gemini、Anthropic、Responses 需要各自协议实现。

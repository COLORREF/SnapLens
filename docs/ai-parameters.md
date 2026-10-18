# AI 参数与模型配置

SnapLens 当前使用 **OpenAI Chat Completions 兼容格式**，请求端点为 `/chat/completions`。填写其他地址并不会将协议自动变成 Responses、Anthropic Messages 或 Gemini 原生接口；这些接口需要对应的兼容网关。服务商预设只提供地址、默认模型和字段形式建议，不再给整个服务商声明一套模型能力。

## 自动与自定义

温度、Top P、频率惩罚、存在惩罚、随机种子和最大输出 Token 默认不发送。勾选对应的“自定义（发送）”才写入请求。数字 0 是合法的显式值，不再充当“不指定”的标记。

思考模式、采样开关和推理强度的 auto 表示不发送。空停止序列不发送；非空序列按行发送，不再截断到统一的四项。流式用量统计默认关闭，显式开启才附加 stream_options。

“最大输出 Token”是一次生成的输出预算，不是上下文窗口。上游返回的上下文窗口是模型容量信息；本应用的单次翻译没有聊天历史压缩流程，不提供能“扩大模型容量”的虚假上下文参数。

数值控件提供常见输入范围，不代表当前模型支持这些范围。模型的具体限制由上游校验；不同范围或其他参数可通过高级 JSON 设置。不会再按服务商截断输出预算、缩小温度、隐藏整家服务商的字段或将推理档位转换为相邻档位。

## 思考参数

推理强度下拉框是可编辑输入框。点击“获取模型与能力”后优先展示当前端点、当前模型返回的档位；没有声明时，对少数可精确匹配的官方端点和型号使用注明来源的模型文档候选，其余显示通用候选。上游明确返回空档位列表或不支持推理时，不再把通用档位当作推荐项。已保存的显式值始终保留，即使不在新列表中，也不会偷偷替换成相邻档位。非 auto 字符串原样写入 reasoning_effort。

上游声明的默认档位仅展示，不会填进请求。用户选择 auto 时仍然不发送，让实际处理请求的上游使用当时的默认值。“没有发送”“显式关闭”“最低强度”不是同一件事；尤其不能把任意上游的 off 自动映射成 none、false 或省略。

思考开关支持两种字段形式：

- thinking.type = enabled / disabled
- enable_thinking = true / false

字段形式的优先级为：用户显式选择 > 当前端点/模型的明确声明 > 服务商预设。supported_parameters 中的 enable_thinking 可以提示布尔开关字段，但不能证明模型同时允许开启和关闭；仅有 supports_reasoning=true 或 thinking 参数名不能推断开关形式。

如果网关提供 parameter_schema.properties（JSON Schema 扩展），可从 enable_thinking 的 boolean 类型及 enum/const，或无需其他必填属性的 thinking.type 枚举，提取简单开关和可选模式。预算型 thinking 要求 budget_tokens 时不会被误判成简单开关。该扩展不是 OpenAI /models 的标准必有字段。

自定义服务及无默认形式的服务商在资料不足时不会猜测格式；显式开启/关闭会得到配置错误提示。对于预算型思考或其他嵌套形式，请把普通思考控件保留为自动，在高级 JSON 中填写。

## 翻译界面的快捷选择

文本翻译和截图翻译共用模型、思考开关和推理档位控件。模型列表来自当前接口的缓存及自定义候选，也可直接输入模型 ID；获取或更新模型列表仍在 AI 设置页进行。切换模型先加载该端点/模型的完整参数配置，避免沿用另一个模型的高级 JSON 或数值参数。

默认“按模型设置”完整继承配置（包括高级 JSON）。显式选择思考或档位时，只覆盖本次请求对应的思考字段；例如档位选择 auto 会移除高级 JSON 中的 reasoning_effort。其他高级参数保持原样，无效 JSON 仍由请求构建器报错。快捷选择按窗口、模型暂存，不保存为全局配置，运行中调整只影响下一次翻译。可以点击“停止翻译”取消正在等待或生成的请求。

## 超时与取消

旧 ai_timeout 从“整次请求总时限”改为“连接与发送超时”：覆盖排队、DNS、TLS 和发送请求，默认 30 秒；请求发出后停止这一阶段的计时，不再限制推理和生成总时长。

新增“响应空闲超时”（ai_response_idle_timeout，0–3600 秒），默认 0 表示不限。从请求发送完成开始等待，每次收到响应数据（包括 SSE 心跳、思考内容和正文）都重新计时。非流式响应在推理结束前可能一直没有数据，因此需要长推理时保持 0。界面无法判断没有数据时模型是在思考还是服务无响应；需要中止时点击停止。两种传输模式均支持取消，供应商或代理自身的超时仍可能终止连接。

获取模型资料使用独立、有限的超时，不受生成等待选项影响。旧配置不需要迁移数值，但 ai_timeout 的含义已改变；新空闲超时默认不限，避免升级后继续误伤长时间思考。

## 自定义 JSON 与预览

JSON 必须是对象。它在普通控件生成请求后应用：

- 同名顶层字段覆盖普通控件值，嵌套对象整体替换。
- 顶层 null 是本地删除标记：删除该字段，不向接口发送 null。
- model、messages、stream 由应用管理，不能覆盖。
- max_tokens 和 max_completion_tokens 不能同时存在；出现冲突会提示修正。
- 非流式请求始终移除 stream_options。
- JSON 语法错误、受保护字段冲突及无效输出预算会在网络请求前失败，流式与非流式共用校验逻辑。
- 不自动删除服务器拒绝的参数，也不通过静默重试改变用户意图。

例如，覆盖输出字段并设置厂商专用参数：

```json
{
  "max_tokens": null,
  "max_completion_tokens": 8192,
  "reasoning_effort": "high"
}
```

“刷新请求预览”使用真正发送请求的构建函数，消息内容使用示例文本，不发起网络请求。修改参数后旧预览会清空。预览不包含鉴权头；不要把密钥填进自定义 JSON。

## 按端点和模型保存

所有上述请求选项按规范化的 Chat Completions 地址和模型 ID 分别保存：

- 地址末尾斜杠不影响配置身份，地址路径大小写不被改写。
- 切换模型/地址暂存编辑值，再加载该模型的配置。
- 新模型使用自动参数；切回旧模型恢复其设置。
- 点击“保存设置”统一落盘；“放弃修改”恢复已保存的配置。
- AI 页面恢复默认会清空模型参数配置和缓存的模型资料。

旧版本没有记录用户是否真的修改过数值滑块。因此升级时保留滑块值，但数值参数默认不勾选发送；旧的显式思考、停止序列和流式统计选择继续保留。请通过预览核对后保存。

## 模型元数据

获取模型列表时保留 data[] 中各模型的原始元数据，并按接口地址缓存。也支持兼容网关的 models 对象形式，此时对象键是实际请求模型 ID，不采用内部可能不同的 id。data 数组优先；无有效清单结构会报错，不伪装成成功。

统一解析层 ModelCapabilities 识别以下明确声明，未声明和无效类型保持“未知”：

| 信息 | 兼容字段 |
| --- | --- |
| 总上下文窗口 | context_length、context_window、contextWindow、limit.context |
| 最大输入 | max_input_tokens、maxInputTokens、limit.input |
| 最大输出 | top_provider.max_completion_tokens、max_output_tokens、maxOutputTokens、maxTokens、limit.output |
| 推理能力 | supports_reasoning、supportsReasoning、布尔 reasoning |
| 推理档位 | DeepSeek 的 effort.supported_levels；supported_reasoning_efforts、reasoning_efforts、reasoningLevels、reasoningEfforts、reasoning.efforts、parameter_schema 的 reasoning_effort.enum、reasoning_options 中 type=effort 的 values |
| 默认档位（只展示） | DeepSeek 的 effort.default_level；reasoningDefaultLevel、reasoning.defaultEffort、reasoning_effort schema 的 default |
| 参数名 | supported_parameters |

这些是多个兼容网关或模型目录的扩展字段，并非任何一家都提供的统一标准。支持包含 id 的档位对象；档位名称不改大小写、不映射为本地固定等级。容量必须是有效正整数。最大输入不再冒充“总上下文窗口”，输出预算也不会从模型容量自动填入请求。

2026-10-17 仅通过两家官方 `GET /models` 校验：DeepSeek 返回 `deepseek-flash`、`deepseek-v4-pro`，各自声明 `effort.supported_levels = [low, high, max]`、默认 `high`，以及上下文 1,048,576、最大输出 393,216；GLM 的 11 个模型条目只有 ID、创建时间和归属，未提供档位或容量。因此 GLM 未显示档位并不表示模型没有档位。对精确匹配 `https://open.bigmodel.cn/api/paas/v4` 的 `glm-5.3`、`glm-5.3-flash`，设置页参考模型作者文档展示 low/high/max（默认 max）及仅可开启思考；`glm-5.2` 展示 high/max（默认 max）。这批文档候选不会写入上游元数据缓存、不会被当作接口声明，也不会自动发送。其他地址、代理及未明确记录的型号仍保持未知。文档候选可能落后于平台实际行为，用户可保留手动设置并查看请求预览。

当参数清单明确只声明 max_tokens / max_completion_tokens 之一时，“自动”输出字段采用该形式；两者都声明或都没声明时使用原有预设。显式字段选择和高级 JSON 仍优先。

这些信息只是已获取的接口声明，不是参数组合有效性的完整 schema，也不保证代理路由到的每个后端拥有相同能力。缓存可通过重新获取模型列表刷新；刷新失败保留上一份缓存及获取时间，完全没有资料时回落通用候选。不会在每次翻译时额外联网探测，也不会因为网络失败擅自把用户的显式配置重置成 auto。

预览和两条翻译路径使用同一份按端点/模型选出的元数据快照，字段建议一致。上游资料只影响候选项和用户选择“自动”时的字段形式，不会修改已填写的数值、启用未勾选的参数或发送上游默认档位。不从模型名字猜测容量，不把“没返回字段”当成“不支持”。超过缓存输出上限或当前档位不在声明中时提供提示，保留用户的显式配置。

目前连接的是 Chat Completions 兼容接口，不会向 Gemini 或 Anthropic 原生接口误发 OpenAI 格式请求。以后增加原生协议时，应分别实现请求、流式响应和元数据适配器。

参考资料：
- [OpenAI 模型接口](https://developers.openai.com/api/reference/cli/resources/models/methods/retrieve)
- [OpenAI 推理模型](https://developers.openai.com/api/docs/guides/reasoning)
- [OpenRouter 模型元数据](https://openrouter.ai/docs/api/api-reference/models/get-models)
- [Gemini 模型元数据](https://ai.google.dev/api/models)
- [Anthropic 模型元数据](https://platform.claude.com/docs/en/api/models/retrieve)
- [DeepSeek 模型列表及档位字段](https://api-docs.deepseek.com/api/list-models/)
- [GLM-5 系列模型作者文档](https://github.com/zai-org/GLM-5)

## 回归验证

运行 SnapLens.exe --regression-checks。测试使用本机 HTTP fixture，不需要真实 API Key，不消耗供应商额度。覆盖默认字段省略、显式零值、自定义推理档位、JSON 覆盖/删除与保护字段、输出字段冲突、错误发送前拦截、元数据保留，以及模型参数隔离和保存恢复。

长推理回归使用本地延迟响应、分段思考与心跳，验证超过连接超时仍成功、可选空闲超时、无限等待可取消、模型列表超时，以及快捷控件与高级 JSON 的覆盖和模型隔离。

能力发现回归还覆盖：未知与空列表区分、非法元数据、固定开关、预算型思考、通用档位回退、网关 models 映射、上游字段建议及显式覆盖、设置页刷新失败保留缓存、动态候选保留自定义值，以及保存恢复后运行时使用正确模型资料。

## 开源实现参考与取舍

本次按源码比较以下实现，没有引入其运行时依赖：

- [DeepSeek Harness 模型能力](https://github.com/deepseek-ai/deepseek-harness/blob/86a402200d95fecef4529d969836f1fe13c6cb0d/packages/llm/llm-deepseek/src/model-info.ts)：能力来自适配器和部署配置，不等于都从远程 API 获得。档位是模型自己的标识符，默认档位和模型能力分开。
- [DeepSeek Harness / pi-ai 模型发现](https://github.com/deepseek-ai/deepseek-harness/blob/86a402200d95fecef4529d969836f1fe13c6cb0d/packages/llm/llm-pi-ai/src/discovery.ts)：已知协议/目录与自定义端点分开，支持 data 数组和 models 映射，以及不同容量字段。SnapLens 参考了声明归一和身份隔离，没有照搬其内置目录或默认容量。
- [DeepSeek Harness / pi-ai 适配器](https://github.com/deepseek-ai/deepseek-harness/blob/86a402200d95fecef4529d969836f1fe13c6cb0d/packages/llm/llm-pi-ai/src/adapter.ts)：目录容量不自动变成请求输出预算，未声明的默认推理参数不发送。SnapLens 采用这一分离；对未知代理仍保留手动参数，而不按可能过期的目录强制拒绝。
- [OpenCode 模型目录](https://github.com/anomalyco/opencode/blob/7b99646cf1f8250b900aa02427ea5fa6e63f52d1/packages/core/src/models-dev.ts)：从 models.dev 维护模型能力、容量和 reasoning_options，并有缓存/快照。SnapLens 识别相应扩展形态，但目前不额外请求第三方目录，以免把原厂模型能力误认为当前代理通道的能力。
- [agent2api 元数据补齐](https://github.com/aimod-cc/agent2api/blob/a55e1eb1133685d578ea997fc0cf701b3b445716/desktop-tauri/src-tauri/server/src/server/core/model_meta/fill.rs)：区分上游、用户和外部目录来源；能力信息与真正发送字段的适配逻辑分离。其管理页的 reasoningLevels 不代表公开 /v1/models 一定返回该字段。
- [Continue 模型配置与发现](https://github.com/continuedev/continue/blob/main/core/config/yaml/models.ts)：模型发现和用户配置分别处理，允许模型级覆盖；自动发现模型名并不意味着获得了所有参数的有效范围。

后续若加入第三方模型目录，应单独标明来源和更新时间、精确匹配服务商及模型，且只补展示资料。不要按模型名模糊匹配后自动发送思考字段；不要将各家参数重试删除到“请求能成功”为止。

## 模块复用

网络与模型资料模块已独立为仅依赖 Qt Core/Network 的 `SnapLens::Ai` CMake 目标；接入示例、线程约束和应用配置边界见 [AI 模块复用](ai-reuse.md)。

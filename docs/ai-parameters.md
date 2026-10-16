# AI 参数与服务商适配

各家的「OpenAI 兼容」接口在实际支持的字段上差异很大：同一个请求体发给 A 家正常，发给 B 家可能直接 `400`。本模块按各厂商官方文档声明能力，请求构建时据此裁剪，避免把参数送到不支持它的接口上。

参数依据来源（`providers.h` 中每个服务商的 `docUrl` 即对应链接）：

- DeepSeek：[Chat Completions API](https://api-docs.deepseek.com/zh-cn/api/create-chat-completion)
- 智谱 GLM：[对话补全](https://docs.bigmodel.cn/api-reference/%E6%A8%A1%E5%9E%8B-api/%E5%AF%B9%E8%AF%9D%E8%A1%A5%E5%85%A8)、[深度思考](https://docs.bigmodel.cn/cn/guide/capabilities/thinking.md)
- Kimi：[Chat Completions API](https://platform.kimi.com/docs/api/chat)、[模型参数参考](https://platform.kimi.com/docs/api/models-overview.md)
- 腾讯混元：[OpenAI 兼容接口](https://cloud.tencent.com/document/product/1729/111007)
- 豆包 / 火山方舟：[Chat Completions](https://www.volcengine.com/docs/82379/1494384)
- 通义千问：[OpenAI 兼容接口](https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope)

## 参数矩阵

| 参数 | DeepSeek | GLM | Kimi | 混元 | 豆包 | 通义千问 | OpenAI | 自定义 |
|---|---|---|---|---|---|---|---|---|
| `temperature` | 0–2（思考模式不生效） | 0–1 | **固定，传值报错** | 0–2 | 0–2（部分模型固定 1） | 0–2 | 0–2 | 0–2 |
| `top_p` | 0–1 | 0.01–1 | **固定，传值报错** | 0–1 | 0–1 | 0–1 | 0–1 | 0–1 |
| `frequency_penalty` | **已废弃** | 文档未提供 | **固定 0** | 文档未提供 | -2–2（部分模型不支持） | 文档未提供 | -2–2 | -2–2 |
| `presence_penalty` | **已废弃** | 文档未提供 | **固定 0** | 文档未提供 | -2–2 | -2–2 | -2–2 | -2–2 |
| `seed` | 文档未提供 | 文档未提供 | 文档未提供 | 非 0 正整数，≤10000 | 文档未提供 | 支持 | 支持（Beta） | 支持 |
| `stop` | ≤16 | ≤4 | ≤5 | 支持 | ≤4 | 支持 | ≤4 | 支持 |
| 输出长度字段 | `max_tokens`（≤384K） | `max_tokens`（≤131072） | **`max_completion_tokens`** | `max_tokens` | `max_tokens`/`max_completion_tokens` | `max_tokens` | `max_completion_tokens` | `max_tokens` |
| 采样总开关 | — | `do_sample`（翻译建议 `false`） | — | — | — | — | — | — |
| 思考参数 | `thinking` + `reasoning_effort` | `thinking` + `reasoning_effort` | K2.x `thinking`；K3 `reasoning_effort` | — | `thinking` + `reasoning_effort` | `enable_thinking` | `reasoning_effort` | 可选 `thinking` |

## 实现约定

- 请求体由 `ApiClient::buildRequestBody` 统一构建，按 `ProviderConfig` 裁剪字段；未声明支持的参数**一律不发送**（`auto` 语义），交给服务商默认值，这也是对未知模型最安全的做法。
- 推理强度越界时按 `normalizeReasoningEffort` 就近映射（例如用户选 `none`、而模型只接受 `low/high/max` 时映射为 `low`），避免直接 400。
- 强制思考的模型（GLM-5.3、Kimi K2.7-code）在界面上禁用「关闭思考」，且不会发送 `disabled`。
- 设置页按当前服务商动态显隐从属行与调整区间；顶部提示条说明裁剪原因与文档来源。
- `settings_version` 提升到 2：新增 `ai_do_sample`、`ai_thinking`、`ai_reasoning_effort`、`ai_stop`、`ai_stream_usage`。

## 回归覆盖

`SnapLens.exe --regression-checks` 在本地模拟 HTTP 服务上断言：Kimi 不发送温度/Top P/惩罚项且使用 `max_completion_tokens`；DeepSeek 不发送已废弃的惩罚项但保留温度/Top P 与思考参数；GLM 温度被夹到 ≤1、`do_sample=false` 时不发送温度/Top P、推理档位收敛为 `low/high/max`；通义千问使用顶层 `enable_thinking`；自定义服务保留惩罚项与 `seed`/`stop`。同时校验新参数可保存、停止序列归一化，以及各服务商下相关行的显隐。

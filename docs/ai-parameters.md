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

## 不发送 / 不支持 / 固定参数会怎样

文档对这件事的说明**并不统一，而且相当一部分是"没写"**。逐家核对后的实际情况：

| 情况 | 文档有没有明说 | 依据 |
|---|---|---|
| **不发送参数** | 多数写了默认值；少数只写"有默认" | DeepSeek：`max_tokens` 未设置时非思考默认 8K、思考默认 64K；`thinking.type` 默认 `enabled`；`reasoning_effort` 默认 `high`。GLM：`do_sample` 默认 `true`，`temperature`/`top_p` 各有默认。混元：明确写"模型已有默认参数，**不传值时使用各模型推荐值，不推荐用户修改**"。豆包：`temperature` 默认 1.0、`top_p` 默认 0.7、`max_tokens` 默认 4096、`stream` 默认 false |
| **固定参数传了别的值** | 写了，且**三家的后果分成两类** | **豆包**：`doubao-seed-2-0-pro/lite-260215` 的 `temperature` "手动指定的参数值将被忽略"、`top_p` 固定 0.95 且被忽略——即**静默忽略，不报错**。**GLM**：`do_sample=false` 时 "temperature 和 top_p 参数将被忽略"——同样是**静默忽略**。**Kimi**：参数表把 temperature/top_p/n/惩罚项都标成"**不可改**"，并明确 "传入其他值会**报错**"，正文补充"建议不要显式传入"——即**硬报错** |
| **参数被废弃** | 写了，语义是"无效但不报错" | DeepSeek：`frequency_penalty` / `presence_penalty` "该参数已不再支持。**传入该参数将不会产生任何效果**" |
| **模型不支持某参数** | 只有部分家写，且不写后果 | 豆包：`frequency_penalty`/`presence_penalty` "doubao-seed-2.0 及后续系列……**不支持该参数**"，但没说传了是报错还是忽略。Kimi：K 系列惩罚项固定为 0，传入非 0 会报错 |
| **强制思考却传 `disabled`** | 写了，会报错 | GLM："GLM-5.3 / GLM-5.3-FLASH 不再支持关闭思考（API 请求中 `thinking.type` 传 `disabled` 将会**报错**）"。Kimi K2.7-code："固定为 `all`，**传入其他值报错**" |
| **取值越界** | 部分写了"会报错"，部分是"自动提升" | 豆包 DeepSeek-V4.1-Flash 的 `top_p`："传入值 < 0.95：**系统自动提升至 0.95**"——即**自动纠正而非报错**。GLM-5.3：`reasoning_effort` 只接受 low/high/max，"**其余输入将报错**" |

结论：

1. **"不发送"总体是安全的**，各家都有默认值；这也是本模块 `auto` 语义的由来。唯一要留意的是"默认值未必是翻译想要的默认"——例如 DeepSeek 思考模式默认开启且 `reasoning_effort` 默认 `high`，翻译这类任务未必需要。
2. **"固定参数传了会怎样"没有统一答案**：豆包/GLM 是**静默忽略**，Kimi 是**直接报错**。所以不能靠"反正会被忽略"来兜底，只能按服务商裁剪——这正是 `providers.h` 里把 `temperature`/`topP` 标成 `false` 的原因。
3. **文档普遍没有写"不支持时传了会怎样"**。豆包只说"不支持该参数"，没说后果；这种时候最稳妥的做法就是**不发**，而不是赌它会被忽略。
4. **越界也不统一**：有"自动提升"（豆包 `top_p`）、有"报错"（GLM `reasoning_effort`）。因此本地既做区间夹取，又对枚举做就近映射。

## 实现约定

- 请求体由 `ApiClient::buildRequestBody` 统一构建，按 `ProviderConfig` 裁剪字段；未声明支持的参数**一律不发送**（`auto` 语义），交给服务商默认值，这也是对未知模型最安全的做法。
- 推理强度越界时按 `normalizeReasoningEffort` 就近映射（例如用户选 `none`、而模型只接受 `low/high/max` 时映射为 `low`），避免直接 400。
- 强制思考的模型（GLM-5.3、Kimi K2.7-code）在界面上禁用「关闭思考」，且不会发送 `disabled`。
- 设置页按当前服务商动态显隐从属行与调整区间；顶部提示条说明裁剪原因与文档来源。
- `settings_version` 提升到 2：新增 `ai_do_sample`、`ai_thinking`、`ai_reasoning_effort`、`ai_stop`、`ai_stream_usage`。
- 设置界面把参数分成两层：**常用参数**（输出上限、超时）与**思考与推理**直接平铺；采样、惩罚项、随机种子、停止序列、流式用量收进默认收起的 **高级设置** 折叠区，普通用户无需关心。折叠区展开状态不持久化，每次进入页面都收起。

## 回归覆盖

`SnapLens.exe --regression-checks` 在本地模拟 HTTP 服务上断言：Kimi 不发送温度/Top P/惩罚项且使用 `max_completion_tokens`；DeepSeek 不发送已废弃的惩罚项但保留温度/Top P 与思考参数；GLM 温度被夹到 ≤1、`do_sample=false` 时不发送温度/Top P、推理档位收敛为 `low/high/max`；通义千问使用顶层 `enable_thinking`；自定义服务保留惩罚项与 `seed`/`stop`。同时校验新参数可保存、停止序列归一化，以及各服务商下相关行的显隐。

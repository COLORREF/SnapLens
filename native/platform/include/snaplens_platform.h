// snaplens_platform.h - SnapLens 平台层 C ABI 公共接口
//
// 该头文件定义了 snaplens_platform.dll 导出的所有 C 函数，
// 供 Python 通过 ctypes 调用，也可供未来 C++ 重构版本直接链接。
//
// 所有函数返回值约定：
//   int 类型：1 表示成功，0 表示失败
//   指针参数：out 形式输出，调用方负责分配内存
//
// 线程安全约定：
//   - 生命周期函数（snap_init/snap_shutdown）非线程安全，仅调用一次
//   - 其他函数线程安全，可在任意线程调用
//
#ifndef SNAPLENS_PLATFORM_H
#define SNAPLENS_PLATFORM_H

// 符号导出宏
#ifdef SNAP_PLATFORM_EXPORTS
    #define SNAP_API __declspec(dllexport)
#else
    #define SNAP_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif


// ============================================================================
// 生命周期
// ============================================================================

/* 初始化库。
 * 当前为空操作，保留接口用于未来扩展（如全局 COM 初始化）。
 * 返回：始终 1（成功）。
 */
SNAP_API int snap_init(void);

/* 关闭库。
 * 清理所有热键/钩子资源。可重复调用，幂等。
 */
SNAP_API void snap_shutdown(void);


// ============================================================================
// 全局热键
// ============================================================================

/* Win32 修饰键常量（与 RegisterHotKey 文档一致）
 * 用于 snap_hotkey_register 的 mods 参数位组合
 */
#define SNAP_MOD_ALT      0x0001
#define SNAP_MOD_CONTROL  0x0002
#define SNAP_MOD_SHIFT    0x0004
#define SNAP_MOD_WIN      0x0008
#define SNAP_MOD_NOREPEAT 0x4000  // 按住不重复触发

/* 设置热键触发回调。
 * callback 在 C++ 内部后台线程中被调用（WM_HOTKEY 触发时）。
 * 通过 ctypes 调用时，回调函数会自动获取 GIL，可安全访问 Python 对象。
 *
 * 参数：
 *   callback  - 回调函数指针，签名 void(int hotkey_id, void* user_data)
 *   user_data - 透传给回调的上下文指针（Python 侧传 None）
 */
SNAP_API void snap_hotkey_set_callback(void (*callback)(int, void*), void* user_data);

/* 注册全局热键。
 * 内部在后台消息循环线程中调用 RegisterHotKey，将 WM_HOTKEY 绑定到隐藏窗口。
 *
 * 参数：
 *   hotkey_id - 热键 ID（0x0000-0xBFFF 范围内的唯一值）
 *   mods      - SNAP_MOD_* 位组合
 *   vk        - Win32 虚拟键码（如 0x5A 表示 'Z'）
 * 返回：成功 1，失败 0（键位冲突或键值不支持）
 */
SNAP_API int snap_hotkey_register(int hotkey_id, unsigned int mods, unsigned int vk);

/* 注销热键。 */
SNAP_API void snap_hotkey_unregister(int hotkey_id);


// ============================================================================
// 顶层窗口枚举
// ============================================================================

/* 枚举当前所有可见顶层窗口（排除本进程窗口和被 DWM 遮蔽的隐形窗口）。
 * 结果缓存在内部列表中，通过 snap_window_get_item 逐项获取。
 * 返回：窗口数量。
 */
SNAP_API int snap_window_enum(void);

/* 获取第 idx 个窗口信息（idx 从 0 到 snap_window_enum()-1）。
 *
 * 参数：
 *   idx        - 索引
 *   hwnd       - 窗口句柄输出（long long，避免 64 位句柄截断）
 *   left/top/right/bottom - 物理像素坐标输出（DWM 扩展边框）
 *   title      - 标题输出缓冲区（宽字符）
 *   title_size - 缓冲区大小（字符数，含结束符）
 * 返回：成功 1，越界 0。
 */
SNAP_API int snap_window_get_item(int idx,
                                   long long* hwnd,
                                   int* left, int* top, int* right, int* bottom,
                                   wchar_t* title, int title_size);


// ============================================================================
// 鼠标光标位置
// ============================================================================

/* 获取鼠标在虚拟桌面中的物理像素坐标。
 * 比 Qt 逻辑坐标 × DPR 更精确（非整数 DPR 也无精度损失）。
 */
SNAP_API void snap_cursor_get_pos(int* x, int* y);


// ============================================================================
// Esc 键拦截（WH_KEYBOARD_LL 低级键盘钩子）
// ============================================================================

/* 安装 Esc 键全局拦截。
 * 通过 WH_KEYBOARD_LL 低级键盘钩子，不依赖焦点/键盘抓取即可取消截图。
 * callback 在 C++ 内部后台线程中被调用。
 *
 * 注意：低级钩子有超时限制（默认 300ms），callback 必须快速返回，
 *       只做最小工作（如 emit Qt 信号），不要在回调中执行耗时操作。
 *
 * 参数：
 *   callback  - 回调函数指针，签名 void(void* user_data)
 *   user_data - 透传给回调的上下文指针
 * 返回：成功 1，失败 0（已安装或钩子安装失败）
 */
SNAP_API int snap_cancel_install(void (*callback)(void*), void* user_data);

/* 卸载 Esc 拦截。可重复调用，幂等。 */
SNAP_API void snap_cancel_uninstall(void);


// ============================================================================
// 光标移动限制
// ============================================================================

/* 将光标移动限制在指定的物理像素矩形内。
 * 用于截图时防止光标漂移到其他屏幕。
 * 返回：成功 1，失败 0。
 */
SNAP_API int snap_clip_cursor(int left, int top, int right, int bottom);

/* 解除光标限制，恢复正常移动范围。 */
SNAP_API void snap_clip_cursor_release(void);


#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SNAPLENS_PLATFORM_H

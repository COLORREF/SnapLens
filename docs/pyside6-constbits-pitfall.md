# PySide6 QImage.constBits() 踩坑记录

> 2026-08-04 | SnapLens OCR 像素直传重构时踩坑

---

## 问题

OCR 像素直传需要从 `QImage.constBits()` 读取原始像素数据传给 C DLL，但不同 PySide6 版本返回类型不一致：

| PySide6 版本 / 场景 | `constBits()` 返回类型 | `bytes()` | `int()` | `asarray()` |
|---|---|---|---|---|
| 某些 6.1.x | `QByteArray` | ✅ | ❌ | ❌ |
| 某些 6.3-6.5 | `bytes` | ✅ | ❌ | ❌ |
| 某些 6.6+ | `sip.voidptr` | ❌ | ✅ | ✅ |
| 某些打包环境 | `sip.voidptr` (无 `asarray`) | ❌ | ✅ | ❌ |

**三个分支必须覆盖全，缺一个就崩。**

---

## 错误表现

```
invalid literal for int() with base 10: b'\x19\x1a\x1b\x19\x1a\x1b...'
```

当 `constBits()` 返回 `QByteArray` 时：
- `isinstance(qba, (bytes, bytearray))` → `False`
- `hasattr(qba, 'asarray')` → `False`  
- 掉进 `int(qba)` → 炸

---

## 最终方案（`core/ocr.py`）

```python
bits_ptr = image.constBits()
try:
    data = bytes(bits_ptr)          # QByteArray / bytes / bytearray → ok
except (TypeError, ValueError):
    if hasattr(bits_ptr, 'asarray'):  # sip.voidptr with asarray
        data = bytes(bits_ptr.asarray(byte_count))
    else:                             # bare void*
        import ctypes as _ct
        ptr = _ct.cast(int(bits_ptr), _ct.POINTER(_ct.c_ubyte))
        data = _ct.string_at(ptr, byte_count)
```

**核心思路**：`bytes()` 是 Python 内置，能统一处理 `QByteArray` / `bytes` / `bytearray` / `memoryview`，只有真正的 void 指针才会失败掉进回退路径。

---

## 教训

1. **PySide6/Shiboken 在跨版本时，Qt 类型的 Python 绑定行为不保证一致**。`constBits()` 的返回值在官方文档中写的是 `uchar*`，但 Python 侧的实际封装依赖 Shiboken 版本。
2. **用 `try/except` 替代 `isinstance` 类型检测更健壮**，因为 Shiboken 包装类的继承关系在不同版本可能不同。
3. **永远不要假设 `int(voidptr)` 能工作** — 某些包装类不支持 `__int__()`。

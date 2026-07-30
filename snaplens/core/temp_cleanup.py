"""临时文件清理模块。

提供统一的 temp 目录清理逻辑，供 app.py 启动流程和各结果窗口共享。

重要提示给用户：清理操作会删除 temp 目录下的所有文件和子目录，
请勿将重要文件放入该目录。
"""
import logging
import os
import shutil

_log = logging.getLogger(__name__)


def cleanup_temp_dir(temp_dir: str) -> int:
    """清理临时目录中的所有文件和子目录。

    会递归删除 temp 目录下的所有内容，不做期限过滤。
    用户应将 temp 目录仅用于程序自动生成的临时文件。

    Args:
        temp_dir: 临时目录路径。

    Returns:
        实际删除的条目（文件+子目录）数量。
    """
    if not temp_dir or not os.path.isdir(temp_dir):
        return 0

    removed = 0

    try:
        for entry in os.scandir(temp_dir):
            try:
                if entry.is_file() or entry.is_symlink():
                    os.unlink(entry.path)
                elif entry.is_dir():
                    shutil.rmtree(entry.path)
                removed += 1
            except OSError:
                pass
    except OSError:
        pass

    if removed:
        _log.info(
            "temp cleanup: removed %d item(s) from %s",
            removed, temp_dir,
        )
    return removed

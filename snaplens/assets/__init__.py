"""编译后的 Qt 资源文件（图标等静态资源）。
资源通过 assets.qrc → pyside6-rcc → assets_rc.py 编译嵌入。
使用方式：import snaplens.assets.assets_rc 后即可通过 QIcon(":/name.svg") 访问。
"""

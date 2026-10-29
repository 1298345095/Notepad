# 构建、验证与分发

## 依赖模式

默认的 `NOTEPAD_FETCH_WX=OFF` 使用本机 wxWidgets 3.2 或更新版本的 `core`、`base`、`stc` 组件。适合 Linux 发行版依赖和已有 wx 开发环境。

`NOTEPAD_FETCH_WX=ON` 从 [wxWidgets 3.2.12 官方发布](https://github.com/wxWidgets/wxWidgets/releases/tag/v3.2.12) 获取 `wxWidgets-3.2.12.tar.bz2`，静态编译所需 wx 组件。使用完整发布包是因为 GitHub 自动生成的 tag source archive 不包含全部第三方子模块。CMake 固定以下 SHA-256：

```text
a62719bb5e1dcc41c1a6fc1eecd499c7ee8521f70402c7b1bb3342a1e16344e0
```

构建关闭 WebView、媒体、富文本、Ribbon、AUI、PropertyGrid、XRC、HTML 和 OpenGL 等本原型不使用的模块。保留 Scintilla 和基础桌面功能。此配置用于控制依赖，包体积和性能仍须以实际构建测量为准。

## Linux

Ubuntu 24.04 使用发行版 wxWidgets：

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build libwxgtk3.2-dev xvfb xauth
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Notepad
```

固定源码构建则安装 GTK 开发依赖并开启 FetchContent：

```sh
sudo apt-get install libgtk-3-dev libnotify-dev libcurl4-openssl-dev pkg-config dpkg-dev
cmake -S . -B build-pinned -G Ninja -DNOTEPAD_FETCH_WX=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-pinned --parallel
ctest --test-dir build-pinned --output-on-failure
```

## Windows

安装 Visual Studio 2022 的“使用 C++ 的桌面开发”组件和 CMake。在开发者终端中：

```powershell
cmake -S . -B build -A x64 -DNOTEPAD_FETCH_WX=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\Notepad.exe
```

这一路径将 wxWidgets 和 MSVC CRT 静态链接到应用。使用本机 wx 动态库的构建需要另外分发相应 DLL，不能只复制 EXE。

## macOS

安装 Xcode Command Line Tools 和 CMake。下面的命令构建当前机器架构：

```sh
cmake -S . -B build -DNOTEPAD_FETCH_WX=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/Notepad.app
```

CI 使用 Apple Silicon 的 `macos-15` 运行器并指定 `-DCMAKE_OSX_ARCHITECTURES=arm64`。Intel Mac 可在对应机器构建 `x86_64`；当前 CI artifact 不包含 Intel 架构，也不是 Universal Binary。包尚未配置 Developer ID 签名和公证。

## 只构建核心测试

此模式不查找或下载 wxWidgets，也不需要图形桌面：

```sh
cmake -S . -B build-core -DNOTEPAD_BUILD_GUI=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release --parallel
ctest --test-dir build-core -C Release --output-on-failure
```

两个 CTest 程序为 `document_io_test` 和 `diff_lexer_test`。Release 配置下仍启用测试中的断言。

## GUI smoke 与加载测量

在 GUI 构建目录中运行对应可执行文件：Linux 为 `build/Notepad`，Windows 为 `build/Release/Notepad.exe`，macOS 为 `build/Notepad.app/Contents/MacOS/Notepad`。

```sh
./build/Notepad --smoke-test --smoke-output "$PWD/smoke.json"
python3 tools/generate_fixture.py fixtures/10m.txt --size-mib 10
./build/Notepad --benchmark "$PWD/fixtures/10m.txt" --benchmark-output "$PWD/benchmark.json"
python3 tools/generate_fixture.py fixtures/long-line.txt --size-mib 10 --kind long-line
```

Linux 无桌面时：

```sh
xvfb-run -a ./build/Notepad --smoke-test --smoke-output "$PWD/smoke.json"
```

测试数据以 MiB（1,048,576 字节）计，保持有效 UTF-8，生成器会替换指定输出文件。普通短行和单一超长行分别生成，避免仅按文件大小比较不同工作负载。

GUI smoke 的进程退出状态和 JSON 应同时检查。输入法候选框、中文预编辑、系统剪贴板、DPI 与字体显示需要真实目标桌面验收。CI 基准不提供跨机器横向性能结论；比较原版与原型时，应固定机器、文件、编译模式、语法着色和自动换行设置。

## 安装与打包

```sh
cmake --install build --config Release --prefix "$PWD/stage"
cpack --config build/CPackConfig.cmake -C Release -B dist
```

Windows/macOS 默认生成 ZIP。Linux 默认生成 DEB，需要 `dpkg-shlibdeps`，通常由 `dpkg-dev` 提供；依赖根据实际链接的系统共享库生成。

CI 在打包前运行暂存目录中的应用，检查运行包本身的基本功能。下载的 Linux 包以 Ubuntu 24.04 为构建基线，应用通过系统 GTK 库显示窗口；其他发行版可使用本机依赖从源码构建。

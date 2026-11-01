# CUDA 弱光视频增强

基于 C++、OpenCV CUDA 和多线程流水线的弱光视频增强项目，支持参考图融合、视频预览和增强前后上下对比视频输出。保留原项目可执行文件名称 `AirportEnhancement_remake_`。

## 实现方式

- 视频读取 → 4 个增强处理线程 → 按帧号顺序输出。
- 输入图像宽高各缩小 4 倍，在 GPU 上进行 HSV 亮度增强：对数变换、幂次变换、归一化和 CLAHE。
- 与参考图进行迭代像素融合，再逐通道归一化、放大并下载到 CPU。
- 输出视频中上方为原图、下方为增强图，并叠加帧率信息。

参考图会直接参与像素融合。应使用同场景、视角尽量一致的参考图；更换视频时需要重新检查参考图。尺寸调整不等于图像配准，输入不匹配可能出现重影、错误纹理或颜色变化。性能与效果需在具体视频和硬件上测量。

## 源码

| 文件 | 用途 |
| --- | --- |
| `main.cpp` | 文件视频的解码、并行增强、按序预览和保存 |
| `Enlighten.cpp` / `Enlighten.h` | OpenCV CUDA 单帧增强 |
| `camera_realtime/main_camera.cpp` | Windows 摄像头实时增强 |
| `camera_realtime/capture_reference.cpp` | 摄像头参考图采集及相机参数保存 |
| `probe_cameras.py` | 可选：通过 OpenCV Python 探测摄像头 |

## 构建

当前代码面向 Windows，使用 C++17、CMake 3.18+、Visual Studio C++ 编译工具和 NVIDIA GPU。摄像头入口使用 Windows DirectShow。

需要已启用 CUDA 的 OpenCV，包含 `core`、`imgproc`、`imgcodecs`、`highgui`、`videoio`、`cudaarithm`、`cudafilters`、`cudaimgproc`、`cudawarping` 模块。普通 CPU 版 OpenCV 不能代替 CUDA 构建；当前增强流程没有完整 CPU 回退。运行时还需要对应的驱动、CUDA 和 OpenCV DLL 可被加载。

直接指定已安装的 OpenCV 配置目录（PowerShell）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DOpenCV_DIR=C:/path/to/opencv/lib/cmake/opencv4"
cmake --build build --config Release
```

如果使用已配置好 CUDA OpenCV 的 vcpkg 环境，也可以显式传入工具链；先把 `VCPKG_ROOT` 设置为自己的 vcpkg 目录：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

两种方式选择一种。工具链在首次配置时指定，切换依赖环境时使用新的构建目录。

## 处理文件视频

从仓库根目录运行，三个位置参数依次为输入视频、参考图、输出 MP4：

```powershell
New-Item -ItemType Directory -Force results | Out-Null
.\build\Release\AirportEnhancement_remake_.exe "C:/path/to/input.mp4" "C:/path/to/reference.png" "results/compare.mp4"
```

事先创建输出父目录，使用新的输出文件名。程序会显示预览窗口，可按 Esc 停止；适合有图形桌面的会话。无参数运行时仍使用原来的机场示例相对路径，这些媒体未包含在仓库中。`run_dark_pic1L_realtime.cmd` 也依赖自行准备的机场媒体。

## 摄像头入口

在实际连接摄像头的 Windows 桌面会话中，先在正常光照下运行 `capture_camera_reference.cmd`，再运行 `run_camera_realtime.cmd`。默认使用摄像头 0、1920×1080、30 FPS；实际支持情况由设备决定。

查看可用参数：

```powershell
.\build\Release\AirportEnhancementCaptureReference.exe --help
.\build\Release\AirportEnhancementCameraRealtime.exe --help
```

带 `_smoke` 的脚本会实际访问摄像头并生成文件，按需手动执行。摄像头参数文件、参考图和录像保存在本机数据目录，不纳入版本控制。

## 数据与版本说明

本仓库仅保存源码、启动脚本和文档。输入视频、参考图片、结果、日志、模型文件、第三方依赖和构建产物均由 `.gitignore` 排除，请在计算机器上自行保存。

首次导入来自现有 Windows 项目：增强算法及视频/摄像头 C++ 源码保持原字节；仅移除了 CMake 中固定的本机依赖路径，并让启动脚本相对自身目录工作。未经许可未另行附加开源许可证。

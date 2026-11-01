#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

#include "Enlighten.h"

struct Options {
    int cameraIndex = 0;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    int factor = 4;
    bool preview = true;
    bool record = true;
    bool autofocus = true;
    int durationSeconds = 0;
    double previewScale = 0.42;
    double temporalSmoothing = 0.35;
    std::string referencePath = "CANKAO/camera_reference_latest.png";
    std::string outputPath = "outvideo_camera_realtime/camera_pic1L_realtime.mp4";
    std::string settingsPath = "CANKAO/camera_locked_settings.txt";
};

struct FramePacket {
    cv::Mat frame;
    int64_t index = 0;
    int64_t captureTick = 0;
};

struct LatestFrame {
    std::mutex mutex;
    std::condition_variable changed;
    FramePacket latest;
    bool hasFrame = false;
    bool finished = false;
    std::string error;
};

std::atomic<bool> g_run(true);
std::atomic<int64_t> g_capturedFrames(0);
std::atomic<int64_t> g_processedFrames(0);

bool parseBool(const std::string& value) {
    return value == "1" || value == "true" || value == "TRUE" ||
           value == "on" || value == "ON" || value == "yes" || value == "YES";
}

void printUsage() {
    std::cout
        << "AirportEnhancementCameraRealtime options:\n"
        << "  --camera 0\n"
        << "  --width 1920 --height 1080 --fps 30\n"
        << "  --reference CANKAO/camera_reference_latest.png\n"
        << "  --output outvideo_camera_realtime/camera_pic1L_realtime.mp4\n"
        << "  --settings CANKAO/camera_locked_settings.txt\n"
        << "  --preview 1 --record 1 --duration 0\n"
        << "  --autofocus 1 --preview-scale 0.42 --smooth 0.35\n"
        << "\n"
        << "Press ESC or q in the preview window to stop.\n";
}

bool parseArgs(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto needValue = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return false;
        } else if (arg == "--camera") {
            options.cameraIndex = std::stoi(needValue(arg));
        } else if (arg == "--width") {
            options.width = std::stoi(needValue(arg));
        } else if (arg == "--height") {
            options.height = std::stoi(needValue(arg));
        } else if (arg == "--fps") {
            options.fps = std::stod(needValue(arg));
        } else if (arg == "--factor") {
            options.factor = std::stoi(needValue(arg));
        } else if (arg == "--reference") {
            options.referencePath = needValue(arg);
        } else if (arg == "--output") {
            options.outputPath = needValue(arg);
        } else if (arg == "--settings") {
            options.settingsPath = needValue(arg);
        } else if (arg == "--preview") {
            options.preview = parseBool(needValue(arg));
        } else if (arg == "--record") {
            options.record = parseBool(needValue(arg));
        } else if (arg == "--autofocus") {
            options.autofocus = parseBool(needValue(arg));
        } else if (arg == "--duration") {
            options.durationSeconds = std::stoi(needValue(arg));
        } else if (arg == "--preview-scale") {
            options.previewScale = std::stod(needValue(arg));
        } else if (arg == "--smooth") {
            options.temporalSmoothing = std::stod(needValue(arg));
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return true;
}

std::map<std::string, double> readCameraSettings(const std::string& path) {
    std::map<std::string, double> settings;
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cout << "Camera settings file not found: " << path
                  << ". Auto exposure / white balance may remain enabled." << std::endl;
        return settings;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        try {
            settings[key] = std::stod(value);
        } catch (...) {
            std::cout << "Ignored invalid camera setting: " << line << std::endl;
        }
    }

    std::cout << "Loaded camera settings: " << path << std::endl;
    return settings;
}

void setAndReport(cv::VideoCapture& cap, int prop, const std::string& name, double value) {
    if (!std::isfinite(value)) {
        std::cout << "Lock " << name << " skipped: invalid value" << std::endl;
        return;
    }
    const bool ok = cap.set(prop, value);
    std::cout << "Lock " << name << "=" << value
              << ", set=" << (ok ? "true" : "false")
              << ", actual=" << cap.get(prop) << std::endl;
}

void applyLockedCameraSettings(cv::VideoCapture& cap, const Options& options) {
    const auto settings = readCameraSettings(options.settingsPath);
    if (settings.empty()) {
        return;
    }

    // DirectShow commonly uses 0.25 for manual exposure mode. Some cameras
    // ignore this, so each set call is logged with the value reported back.
    setAndReport(cap, cv::CAP_PROP_AUTO_EXPOSURE, "auto_exposure_manual", 0.25);
    setAndReport(cap, cv::CAP_PROP_AUTO_WB, "auto_wb_off", 0.0);

    auto applyIfPresent = [&](const std::string& key, int prop) {
        const auto found = settings.find(key);
        if (found != settings.end()) {
            setAndReport(cap, prop, key, found->second);
        }
    };

    applyIfPresent("exposure", cv::CAP_PROP_EXPOSURE);
    applyIfPresent("gain", cv::CAP_PROP_GAIN);
    applyIfPresent("wb_temperature", cv::CAP_PROP_WB_TEMPERATURE);
    applyIfPresent("brightness", cv::CAP_PROP_BRIGHTNESS);
    applyIfPresent("contrast", cv::CAP_PROP_CONTRAST);
    applyIfPresent("saturation", cv::CAP_PROP_SATURATION);

    setAndReport(cap, cv::CAP_PROP_AUTO_EXPOSURE, "auto_exposure_manual_final", 0.25);
    setAndReport(cap, cv::CAP_PROP_AUTO_WB, "auto_wb_off_final", 0.0);
}

std::string fourccToString(double fourccValue) {
    int value = static_cast<int>(fourccValue);
    std::string text;
    for (int i = 0; i < 4; ++i) {
        char c = static_cast<char>((value >> (8 * i)) & 0xFF);
        text.push_back(c ? c : ' ');
    }
    return text;
}

void captureLoop(const Options& options, LatestFrame& shared) {
    cv::VideoCapture cap(options.cameraIndex, cv::CAP_DSHOW);
    if (!cap.isOpened()) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.error = "Could not open camera index " + std::to_string(options.cameraIndex);
        shared.finished = true;
        shared.changed.notify_all();
        return;
    }

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, options.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, options.height);
    cap.set(cv::CAP_PROP_FPS, options.fps);
    cap.set(cv::CAP_PROP_AUTOFOCUS, options.autofocus ? 1.0 : 0.0);
    applyLockedCameraSettings(cap, options);

    std::cout << "Camera opened: index=" << options.cameraIndex
              << ", requested=" << options.width << "x" << options.height
              << "@" << options.fps << " MJPG" << std::endl;
    std::cout << "Camera actual: "
              << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)) << "x"
              << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT)) << "@"
              << cap.get(cv::CAP_PROP_FPS)
              << ", fourcc=" << fourccToString(cap.get(cv::CAP_PROP_FOURCC))
              << std::endl;

    cv::Mat frame;
    int64_t index = 0;
    int failedReads = 0;

    while (g_run.load()) {
        if (!cap.read(frame) || frame.empty()) {
            ++failedReads;
            if (failedReads > 30) {
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.error = "Camera read failed repeatedly.";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        failedReads = 0;
        ++index;
        g_capturedFrames.store(index, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.latest.frame = frame.clone();
            shared.latest.index = index;
            shared.latest.captureTick = cv::getTickCount();
            shared.hasFrame = true;
        }
        shared.changed.notify_one();
    }

    cap.release();
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.finished = true;
    }
    shared.changed.notify_all();
    std::cout << "Capture stopped. Frames captured: " << index << std::endl;
}

cv::Mat resizeForPreview(const cv::Mat& frame, int targetWidth) {
    const int width = std::max(1, targetWidth);
    const int height = std::max(1, static_cast<int>(
        std::round(static_cast<double>(width) * frame.rows / frame.cols)));
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(width, height));
    return resized;
}

void putMetrics(cv::Mat& frame,
                int64_t cameraIndex,
                double cameraFps,
                double processingFps,
                double realtimeEnhancedFps,
                int64_t droppedFrames,
                const std::string& referencePath) {
    const int x = 40;
    int y = 70;
    const double scale = 1.2;
    const int thickness = 2;
    const cv::Scalar color(0, 255, 255);
    const cv::Scalar red(0, 0, 255);

    cv::putText(frame, "AirportEnhancement Camera Realtime", cv::Point(x, y),
                cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness, cv::LINE_AA);
    y += 55;
    cv::putText(frame, cv::format("Camera Frame : %lld", static_cast<long long>(cameraIndex)),
                cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, scale, red, thickness, cv::LINE_AA);
    y += 55;
    cv::putText(frame, cv::format("Camera FPS : %.2f", cameraFps),
                cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, scale, red, thickness, cv::LINE_AA);
    y += 55;
    cv::putText(frame, cv::format("Enhanced Processing FPS : %.2f", processingFps),
                cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, scale, red, thickness, cv::LINE_AA);
    y += 55;
    cv::putText(frame, cv::format("Realtime Enhanced FPS : %.2f", realtimeEnhancedFps),
                cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, scale, red, thickness, cv::LINE_AA);
    y += 55;
    cv::putText(frame, cv::format("Dropped Camera Frames : %lld", static_cast<long long>(droppedFrames)),
                cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, scale, red, thickness, cv::LINE_AA);
    y += 55;
    cv::putText(frame, "Reference : " + referencePath,
                cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 0.9, red, thickness, cv::LINE_AA);
}

int main(int argc, char* argv[]) {
    Options options;
    try {
        if (!parseArgs(argc, argv, options)) {
            return 0;
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        printUsage();
        return 1;
    }

    std::cout << "This is AirportEnhancement camera realtime mode." << std::endl;
    std::cout << "Reference image: " << options.referencePath << std::endl;
    std::cout << "Output video: " << options.outputPath << std::endl;

    cv::Mat reference = cv::imread(options.referencePath);
    if (reference.empty()) {
        std::cerr << "Could not read reference image: " << options.referencePath << std::endl;
        return 1;
    }

    LatestFrame shared;
    std::thread captureThread(captureLoop, std::cref(options), std::ref(shared));

    FramePacket packet;
    {
        std::unique_lock<std::mutex> lock(shared.mutex);
        shared.changed.wait(lock, [&] {
            return shared.hasFrame || shared.finished || !shared.error.empty();
        });
        if (!shared.error.empty()) {
            std::cerr << shared.error << std::endl;
            g_run.store(false);
            lock.unlock();
            captureThread.join();
            return 1;
        }
        if (!shared.hasFrame) {
            std::cerr << "No frame captured." << std::endl;
            g_run.store(false);
            lock.unlock();
            captureThread.join();
            return 1;
        }
        packet = shared.latest;
        shared.hasFrame = false;
    }

    const int frameWidth = packet.frame.cols;
    const int frameHeight = packet.frame.rows;
    cv::Mat referenceResized = reference.clone();
    cv::resize(referenceResized, referenceResized,
               cv::Size(frameWidth / options.factor, frameHeight / options.factor));

    Enlighten enhancer(options.factor, 0.8, 15, true);
    cv::VideoWriter writer;
    if (options.record) {
        std::filesystem::path outputPath(options.outputPath);
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        writer.open(options.outputPath,
                    cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                    options.fps,
                    cv::Size(frameWidth, frameHeight));
        if (!writer.isOpened()) {
            std::cerr << "Could not open output video: " << options.outputPath << std::endl;
            g_run.store(false);
            captureThread.join();
            return 1;
        }
    }

    if (options.preview) {
        cv::namedWindow("AirportEnhancement Camera Realtime", cv::WINDOW_NORMAL);
        const int previewWidth = std::max(1, static_cast<int>(frameWidth * options.previewScale));
        const int previewPanelHeight = std::max(1, static_cast<int>(
            std::round(static_cast<double>(previewWidth) * frameHeight / frameWidth)));
        cv::resizeWindow("AirportEnhancement Camera Realtime", previewWidth, previewPanelHeight * 2);
    }

    const int64_t startTick = cv::getTickCount();
    int64_t lastProcessedCameraIndex = 0;
    int64_t processedCount = 0;
    int64_t droppedFrames = 0;
    int64_t realtimeFpsWindowStartTick = startTick;
    int64_t realtimeFpsWindowFrameCount = 0;
    double realtimeEnhancedFps = 0.0;
    cv::Mat previousEnhanced;

    while (g_run.load()) {
        cv::Mat enhanced;
        enhancer.applyOnImage(packet.frame, referenceResized, enhanced);
        if (enhanced.size() != packet.frame.size()) {
            cv::resize(enhanced, enhanced, packet.frame.size());
        }
        if (!previousEnhanced.empty() &&
            previousEnhanced.size() == enhanced.size() &&
            previousEnhanced.type() == enhanced.type() &&
            options.temporalSmoothing > 0.0) {
            const double previousWeight = std::clamp(options.temporalSmoothing, 0.0, 0.95);
            cv::addWeighted(enhanced, 1.0 - previousWeight,
                            previousEnhanced, previousWeight,
                            0.0, enhanced);
        }
        previousEnhanced = enhanced.clone();

        ++processedCount;
        ++realtimeFpsWindowFrameCount;
        g_processedFrames.store(processedCount, std::memory_order_relaxed);
        if (lastProcessedCameraIndex > 0 && packet.index > lastProcessedCameraIndex + 1) {
            droppedFrames += packet.index - lastProcessedCameraIndex - 1;
        }
        lastProcessedCameraIndex = packet.index;

        const int64_t nowTick = cv::getTickCount();
        const double elapsed = static_cast<double>(nowTick - startTick) / cv::getTickFrequency();
        const double cameraFps = elapsed > 0.0 ? static_cast<double>(g_capturedFrames.load()) / elapsed : 0.0;
        const double processingFps = elapsed > 0.0 ? static_cast<double>(processedCount) / elapsed : 0.0;
        const double realtimeWindowElapsed =
            static_cast<double>(nowTick - realtimeFpsWindowStartTick) / cv::getTickFrequency();
        if (realtimeWindowElapsed >= 0.5) {
            realtimeEnhancedFps = static_cast<double>(realtimeFpsWindowFrameCount) / realtimeWindowElapsed;
            realtimeFpsWindowStartTick = nowTick;
            realtimeFpsWindowFrameCount = 0;
        } else if (realtimeEnhancedFps <= 0.0 && realtimeWindowElapsed > 0.0) {
            realtimeEnhancedFps = static_cast<double>(realtimeFpsWindowFrameCount) / realtimeWindowElapsed;
        }

        putMetrics(enhanced, packet.index, cameraFps, processingFps, realtimeEnhancedFps,
                   droppedFrames, options.referencePath);

        if (writer.isOpened()) {
            writer.write(enhanced);
        }

        if (options.preview) {
            cv::Mat preview;
            cv::Mat rawLabeled = packet.frame.clone();
            cv::putText(rawLabeled, "Original Camera", cv::Point(40, 70),
                        cv::FONT_HERSHEY_SIMPLEX, 1.3, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            const int previewWidth = std::max(1, static_cast<int>(frameWidth * options.previewScale));
            cv::Mat rawPanel = resizeForPreview(rawLabeled, previewWidth);
            cv::Mat enhancedPanel = resizeForPreview(enhanced, previewWidth);
            cv::vconcat(rawPanel, enhancedPanel, preview);
            cv::imshow("AirportEnhancement Camera Realtime", preview);
            const int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') {
                std::cout << "Stop key pressed." << std::endl;
                g_run.store(false);
                break;
            }
        }

        if (options.durationSeconds > 0 && elapsed >= options.durationSeconds) {
            std::cout << "Duration reached: " << options.durationSeconds << " seconds." << std::endl;
            g_run.store(false);
            break;
        }

        {
            std::unique_lock<std::mutex> lock(shared.mutex);
            shared.changed.wait(lock, [&] {
                return !g_run.load() || shared.hasFrame || shared.finished || !shared.error.empty();
            });
            if (!shared.error.empty()) {
                std::cerr << shared.error << std::endl;
                g_run.store(false);
                break;
            }
            if (!shared.hasFrame && shared.finished) {
                break;
            }
            if (shared.hasFrame) {
                packet = shared.latest;
                shared.hasFrame = false;
            }
        }
    }

    g_run.store(false);
    shared.changed.notify_all();
    captureThread.join();
    if (writer.isOpened()) {
        writer.release();
    }
    if (options.preview) {
        cv::destroyWindow("AirportEnhancement Camera Realtime");
    }

    std::cout << "Camera realtime finished. Captured="
              << g_capturedFrames.load()
              << ", processed=" << processedCount
              << ", dropped=" << droppedFrames
              << std::endl;
    return 0;
}

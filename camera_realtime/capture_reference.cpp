#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

struct Options {
    int cameraIndex = 0;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    bool preview = true;
    bool autofocus = true;
    int autoCaptureSeconds = 0;
    double previewScale = 0.5;
    std::string outputPath = "CANKAO/camera_reference_latest.png";
    std::string backupDir = "CANKAO";
    std::string settingsPath = "CANKAO/camera_locked_settings.txt";
};

bool parseBool(const std::string& value) {
    return value == "1" || value == "true" || value == "TRUE" ||
           value == "on" || value == "ON" || value == "yes" || value == "YES";
}

void printUsage() {
    std::cout
        << "AirportEnhancementCaptureReference options:\n"
        << "  --camera 0\n"
        << "  --width 1920 --height 1080 --fps 30\n"
        << "  --output CANKAO/camera_reference_latest.png\n"
        << "  --backup-dir CANKAO\n"
        << "  --settings CANKAO/camera_locked_settings.txt\n"
        << "  --preview 1 --autofocus 1 --preview-scale 0.5\n"
        << "  --auto-capture 0\n"
        << "\n"
        << "Press C or Space to save the reference frame; press ESC or q to cancel.\n";
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
        } else if (arg == "--output") {
            options.outputPath = needValue(arg);
        } else if (arg == "--backup-dir") {
            options.backupDir = needValue(arg);
        } else if (arg == "--settings") {
            options.settingsPath = needValue(arg);
        } else if (arg == "--preview") {
            options.preview = parseBool(needValue(arg));
        } else if (arg == "--autofocus") {
            options.autofocus = parseBool(needValue(arg));
        } else if (arg == "--auto-capture") {
            options.autoCaptureSeconds = std::stoi(needValue(arg));
        } else if (arg == "--preview-scale") {
            options.previewScale = std::stod(needValue(arg));
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return true;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return stream.str();
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

void writeCameraSettings(cv::VideoCapture& cap, const Options& options) {
    const std::filesystem::path settingsPath(options.settingsPath);
    if (settingsPath.has_parent_path()) {
        std::filesystem::create_directories(settingsPath.parent_path());
    }

    const std::vector<std::pair<std::string, int>> props = {
        {"auto_exposure", cv::CAP_PROP_AUTO_EXPOSURE},
        {"exposure", cv::CAP_PROP_EXPOSURE},
        {"gain", cv::CAP_PROP_GAIN},
        {"auto_wb", cv::CAP_PROP_AUTO_WB},
        {"wb_temperature", cv::CAP_PROP_WB_TEMPERATURE},
        {"brightness", cv::CAP_PROP_BRIGHTNESS},
        {"contrast", cv::CAP_PROP_CONTRAST},
        {"saturation", cv::CAP_PROP_SATURATION},
        {"autofocus", cv::CAP_PROP_AUTOFOCUS},
        {"focus", cv::CAP_PROP_FOCUS},
    };

    std::ofstream out(settingsPath);
    if (!out.is_open()) {
        std::cerr << "Could not save camera settings: " << settingsPath.string() << std::endl;
        return;
    }

    out << "# Captured after saving the normal-light reference image.\n";
    out << "# Realtime mode will disable auto exposure / auto white balance and reuse these values.\n";
    out << "width=" << options.width << "\n";
    out << "height=" << options.height << "\n";
    out << "fps=" << options.fps << "\n";

    for (const auto& prop : props) {
        out << prop.first << "=" << cap.get(prop.second) << "\n";
    }

    std::cout << "Saved camera settings: " << settingsPath.string() << std::endl;
}

bool saveReference(const cv::Mat& frame, const Options& options) {
    if (frame.empty()) {
        std::cerr << "No frame to save." << std::endl;
        return false;
    }

    const std::filesystem::path outputPath(options.outputPath);
    const std::filesystem::path backupDir(options.backupDir);
    const std::filesystem::path backupPath =
        backupDir / ("camera_reference_" + timestamp() + ".png");

    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::filesystem::create_directories(backupDir);

    if (!cv::imwrite(outputPath.string(), frame)) {
        std::cerr << "Could not save latest reference: " << outputPath.string() << std::endl;
        return false;
    }
    if (!cv::imwrite(backupPath.string(), frame)) {
        std::cerr << "Could not save backup reference: " << backupPath.string() << std::endl;
        return false;
    }

    std::cout << "Saved latest reference: " << outputPath.string() << std::endl;
    std::cout << "Saved backup reference: " << backupPath.string() << std::endl;
    return true;
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

    cv::VideoCapture cap(options.cameraIndex, cv::CAP_DSHOW);
    if (!cap.isOpened()) {
        std::cerr << "Could not open camera index " << options.cameraIndex << std::endl;
        return 1;
    }

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, options.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, options.height);
    cap.set(cv::CAP_PROP_FPS, options.fps);
    cap.set(cv::CAP_PROP_AUTOFOCUS, options.autofocus ? 1.0 : 0.0);

    std::cout << "Camera opened: index=" << options.cameraIndex
              << ", requested=" << options.width << "x" << options.height
              << "@" << options.fps << " MJPG" << std::endl;
    std::cout << "Camera actual: "
              << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)) << "x"
              << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT)) << "@"
              << cap.get(cv::CAP_PROP_FPS)
              << ", fourcc=" << fourccToString(cap.get(cv::CAP_PROP_FOURCC))
              << std::endl;

    if (options.preview) {
        cv::namedWindow("Capture AirportEnhancement Reference", cv::WINDOW_NORMAL);
        cv::resizeWindow("Capture AirportEnhancement Reference", 1280, 720);
    }

    const int64 startTick = cv::getTickCount();
    cv::Mat frame;
    int failedReads = 0;

    while (true) {
        if (!cap.read(frame) || frame.empty()) {
            ++failedReads;
            if (failedReads > 30) {
                std::cerr << "Camera read failed repeatedly." << std::endl;
                if (options.preview) {
                    cv::destroyWindow("Capture AirportEnhancement Reference");
                }
                return 1;
            }
            cv::waitKey(5);
            continue;
        }
        failedReads = 0;

        const double elapsed =
            static_cast<double>(cv::getTickCount() - startTick) / cv::getTickFrequency();
        if (options.autoCaptureSeconds > 0 && elapsed >= options.autoCaptureSeconds) {
            const bool ok = saveReference(frame, options);
            if (ok) {
                writeCameraSettings(cap, options);
            }
            if (options.preview) {
                cv::destroyWindow("Capture AirportEnhancement Reference");
            }
            return ok ? 0 : 1;
        }

        if (options.preview) {
            cv::Mat preview = frame.clone();
            cv::putText(preview, "Normal-light reference capture", cv::Point(40, 70),
                        cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            cv::putText(preview, "Press C or Space to save. ESC/q cancels.",
                        cv::Point(40, 130), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                        cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
            cv::putText(preview, "Output: " + options.outputPath,
                        cv::Point(40, 190), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                        cv::Scalar(0, 0, 255), 2, cv::LINE_AA);

            if (options.previewScale > 0.0 && options.previewScale != 1.0) {
                cv::resize(preview, preview, cv::Size(), options.previewScale, options.previewScale);
            }
            cv::imshow("Capture AirportEnhancement Reference", preview);

            const int key = cv::waitKey(1);
            if (key == 'c' || key == 'C' || key == ' ') {
                const bool ok = saveReference(frame, options);
                if (ok) {
                    writeCameraSettings(cap, options);
                }
                cv::destroyWindow("Capture AirportEnhancement Reference");
                return ok ? 0 : 1;
            }
            if (key == 27 || key == 'q' || key == 'Q') {
                std::cout << "Reference capture canceled. Existing reference was not changed."
                          << std::endl;
                cv::destroyWindow("Capture AirportEnhancement Reference");
                return 2;
            }
        } else {
            cv::waitKey(1);
        }
    }
}

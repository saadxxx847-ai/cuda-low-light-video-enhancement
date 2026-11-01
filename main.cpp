#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <queue>
#include <string>
#include <algorithm>
#include <opencv2/opencv.hpp>

#include "Enlighten.h"
#define DEBUG 0  //调试打印数据用

const int decodeQueueSize = 100;
const int encodeQueueSize = 100; // 处理数据队列和显示数据队列容纳的最大值

const int frame_factor = 4;
int frame_height, frame_width;
double input_fps = 30.0;

const int64 skip = 100;

std::atomic<bool> run(true);
std::atomic<bool> decodeFinished(false);
std::atomic<bool> processingFinished(false);
std::atomic<int> activeProcessThreads(0);
std::atomic<int64> processedFrameCount(0);
std::atomic<int64> nextEncodeIndex(1);
int64 metricsStartTick = 0;

struct frameInfo {
    std::shared_ptr<cv::Mat> frame_ptr;
    int64 _index;
    frameInfo(std::shared_ptr<cv::Mat> ptr, int64 index) : frame_ptr(ptr), _index(index){}
    // 将传入的shared_ptr<cv::Mat> ptr 赋值给frame_ptr 传入的index赋值给_index 进行初始化
};

struct compareFrameInfo {
    std::shared_ptr<cv::Mat> raw_ptr;
    std::shared_ptr<cv::Mat> enhanced_ptr;
    int64 _index;
    compareFrameInfo(std::shared_ptr<cv::Mat> raw, std::shared_ptr<cv::Mat> enhanced, int64 index)
        : raw_ptr(raw), enhanced_ptr(enhanced), _index(index) {}
};

struct cmp_frame {
    bool operator()(const frameInfo& A, const frameInfo& B){
        return A._index > B._index;
    }
};

struct cmp_compare_frame {
    bool operator()(const compareFrameInfo& A, const compareFrameInfo& B){
        return A._index > B._index;
    }
};

struct ImageQueue {
    std::priority_queue<frameInfo, std::vector<frameInfo>, cmp_frame> imgQueue;
    std::mutex mt; //互斥锁
    std::condition_variable queueNotEmpty, queueNotFull; // 条件变量 队列非空和非满
    int maxSize;
    ImageQueue(int i) : maxSize(i){}
};

struct CompareQueue {
    std::priority_queue<compareFrameInfo, std::vector<compareFrameInfo>, cmp_compare_frame> imgQueue;
    std::mutex mt;
    std::condition_variable queueNotEmpty, queueNotFull;
    int maxSize;
    CompareQueue(int i) : maxSize(i){}
};

ImageQueue decodeQueue(decodeQueueSize); // 初始化处理数据队列
CompareQueue encodeQueue(encodeQueueSize); // 初始化对比显示队列（原图+增强图）

void notifyAllQueues() {
    decodeQueue.queueNotEmpty.notify_all();
    decodeQueue.queueNotFull.notify_all();
    encodeQueue.queueNotEmpty.notify_all();
    encodeQueue.queueNotFull.notify_all();
}

void videoDecode(cv::VideoCapture& cap) {  // cap是一个视频捕获对象
    cv::Mat frame;  //用于存储解码后的图像数据
    int64 index = 0; //用于记录当前帧的索引

    while(run.load()){
        cap >> frame;
        if(frame.empty()) {
            break; // 视频所有帧都读完了
        }
        index++;
//        if((index % skip) == 1) {
//            continue;
//        }

        std::unique_lock<std::mutex> dlock(decodeQueue.mt);
        decodeQueue.queueNotFull.wait(dlock, [] {
            return !run.load() || decodeQueue.imgQueue.size() < decodeQueue.maxSize;
        });
        if(!run.load()) {
            break;
        }
        std::shared_ptr<cv::Mat> frameNew(new cv::Mat(frame.clone()));
        decodeQueue.imgQueue.push(frameInfo(frameNew, index));
        // DEBUG
        if (DEBUG)
        {
            std::cout << "decodeQueue" << '\t'
                      << decodeQueue.imgQueue.size() << '\t'
                      << decodeQueue.maxSize << std::endl;
        }
        // 通知等待线程
        dlock.unlock();
        decodeQueue.queueNotEmpty.notify_all();
    }

    decodeFinished.store(true);
    notifyAllQueues();
    std::cout << "Decode finished. Frames read: " << index << std::endl;
}

void frameProcess(cv::Mat& ref) {
    // 初始化增强算法
    Enlighten es(frame_factor, 0.8, 15, true);
    if(frame_width <= 0 || frame_height <= 0 || ref.empty()) {
        if(activeProcessThreads.fetch_sub(1) == 1) {
            processingFinished.store(true);
            notifyAllQueues();
        }
        return;
    }

    // 每个处理线程使用自己的参考图副本，避免并发写同一个 Mat。
    cv::Mat ref_resized = ref.clone();
    cv::resize(ref_resized, ref_resized, cv::Size(frame_width / frame_factor, frame_height / frame_factor));
    cv::Mat frame, dst;
    int64 index;
    while(true) {
        std::unique_lock<std::mutex> dlock(decodeQueue.mt);
        decodeQueue.queueNotEmpty.wait(dlock, [] {
            return !run.load() || decodeFinished.load() || !decodeQueue.imgQueue.empty();
        });
        if((!run.load() || decodeFinished.load()) && decodeQueue.imgQueue.empty()) {
            break;
        }

        std::shared_ptr<cv::Mat> frame_ptr = decodeQueue.imgQueue.top().frame_ptr;
        index = decodeQueue.imgQueue.top()._index;
        decodeQueue.imgQueue.pop();

        dlock.unlock();
        decodeQueue.queueNotFull.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // 对队列头部的图像帧进行增强
        es.applyOnImage(*frame_ptr, ref_resized, dst);
        processedFrameCount.fetch_add(1, std::memory_order_relaxed);
        std::shared_ptr<cv::Mat> dst_ptr(new cv::Mat(dst.clone()));
        //

        std::unique_lock<std::mutex> elock(encodeQueue.mt);
        encodeQueue.queueNotFull.wait(elock, [index] {
            return !run.load() ||
                   encodeQueue.imgQueue.size() < encodeQueue.maxSize ||
                   index <= nextEncodeIndex.load(std::memory_order_relaxed);
        });
        if(!run.load()) {
            break;
        }

//        std::cout << "save " << index << " size " << decodeQueue.imgQueue.size() << std::endl;
        encodeQueue.imgQueue.push(compareFrameInfo(frame_ptr, dst_ptr, index)); // 把原图和增强图按帧号成对放入显示队列
        elock.unlock();
        encodeQueue.queueNotEmpty.notify_one();
    }

    if(activeProcessThreads.fetch_sub(1) == 1) {
        processingFinished.store(true);
        notifyAllQueues();
        std::cout << "All processing threads finished." << std::endl;
    }
}

std::string globalOutVideoPath = "outvideo_realtime_Dark/pic1L_dark_realtime.mp4";

void videoEncode() {
    cv::namedWindow("AirportEnhancement Realtime Preview", cv::WINDOW_NORMAL);
    cv::resizeWindow("AirportEnhancement Realtime Preview", cv::Size(1400, 700));

    cv::VideoWriter writer;

    int64 outputFrameCount = 0;
    double realtime_output_fps = 0.0;
    double enhanced_processing_fps = 0.0;
    int64 nextDisplayIndex = 1;
    while(run.load()) {
        std::unique_lock<std::mutex> elock(encodeQueue.mt);
        // 严格按帧序显示：只在拿到 nextDisplayIndex 时才出队显示。
        encodeQueue.queueNotEmpty.wait(elock, [&] {
            return !run.load() || processingFinished.load() ||
                   (!encodeQueue.imgQueue.empty() &&
                    encodeQueue.imgQueue.top()._index == nextDisplayIndex);
        });
        if(!run.load()) {
            break;
        }
        if(encodeQueue.imgQueue.empty() && processingFinished.load()) {
            break;
        }
        if(encodeQueue.imgQueue.empty() ||
           encodeQueue.imgQueue.top()._index != nextDisplayIndex) {
            if(processingFinished.load() && !encodeQueue.imgQueue.empty()) {
                std::cerr << "Missing frame " << nextDisplayIndex
                          << ", next available frame is "
                          << encodeQueue.imgQueue.top()._index << std::endl;
                run.store(false);
                notifyAllQueues();
                break;
            }
            continue;
        }
        if (DEBUG)
        {
            std::cout << "encodeQueue" << '\t'
                      << encodeQueue.imgQueue.size() << '\t'
                      << encodeQueue.maxSize << std::endl;
        }

        const int64 currentIndex = encodeQueue.imgQueue.top()._index;
        std::shared_ptr<cv::Mat> rawFrame = encodeQueue.imgQueue.top().raw_ptr;
        std::shared_ptr<cv::Mat> enhancedFrame = encodeQueue.imgQueue.top().enhanced_ptr;
        std::cout << "display " << currentIndex << " size " << encodeQueue.imgQueue.size() << std::endl;
        encodeQueue.imgQueue.pop();
        nextDisplayIndex++;
        nextEncodeIndex.store(nextDisplayIndex, std::memory_order_relaxed);
        elock.unlock();
        encodeQueue.queueNotFull.notify_all();

        if(rawFrame->size() != enhancedFrame->size()) {
            cv::resize(*enhancedFrame, *enhancedFrame, rawFrame->size());
        }

        cv::Mat compareFrame;
        cv::vconcat(*rawFrame, *enhancedFrame, compareFrame);
        int64 nowTick = cv::getTickCount();
        outputFrameCount++;
        double outputElapsedSeconds = (double)(nowTick - metricsStartTick) / cv::getTickFrequency();
        if(outputElapsedSeconds > 0.0) {
            realtime_output_fps = (double)outputFrameCount / outputElapsedSeconds;
        }
        double processingElapsedSeconds = (double)(nowTick - metricsStartTick) / cv::getTickFrequency();
        if(processingElapsedSeconds > 0.0) {
            enhanced_processing_fps = (double)processedFrameCount.load(std::memory_order_relaxed) / processingElapsedSeconds;
        }
        cv::putText(compareFrame, "Original", cv::Point(40, 70),
                    cv::FONT_HERSHEY_SIMPLEX, 1.6, cv::Scalar(0, 255, 255), 3, 8);
        cv::putText(compareFrame, "Enhanced", cv::Point(40, rawFrame->rows + 70),
                    cv::FONT_HERSHEY_SIMPLEX, 1.6, cv::Scalar(0, 255, 255), 3, 8);
        cv::putText(compareFrame, cv::format("Original FPS : %.2f", input_fps), cv::Point(40, 130),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 255), 2, 8);
        cv::putText(compareFrame, cv::format("Realtime Output FPS : %.2f", realtime_output_fps), cv::Point(40, rawFrame->rows + 130),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 255), 2, 8);
        cv::putText(compareFrame, cv::format("Enhanced Processing FPS : %.2f", enhanced_processing_fps), cv::Point(40, rawFrame->rows + 190),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 255), 2, 8);
        cv::putText(compareFrame, cv::format("Output Video FPS : %.0f", input_fps), cv::Point(40, rawFrame->rows + 250),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 255), 2, 8);

        if(!writer.isOpened() && !compareFrame.empty()) {
            writer.open(globalOutVideoPath, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), input_fps, compareFrame.size());
            if(!writer.isOpened()) {
                std::cerr << "Could not open output video: " << globalOutVideoPath << std::endl;
                run.store(false);
                notifyAllQueues();
                break;
            }
        }
        if(writer.isOpened()) {
            writer.write(compareFrame);
        }

        cv::imshow("AirportEnhancement Realtime Preview", compareFrame);
        int ret = cv::waitKey(1);
        if(ret == 27) {
            std::cout << "ESC pressed. Stopping processing." << std::endl;
            run.store(false);
            notifyAllQueues();
            break;
        }

    }
    if(writer.isOpened()) writer.release();
    cv::destroyWindow("AirportEnhancement Realtime Preview");
    std::cout << "Output finished. Frames written: " << outputFrameCount << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "This is a demo for multiThreads of Enlighten!" << std::endl;

    std::string videoPath = "original_Video_of_the_airport/dark.mp4";
    std::string refImage = "CANKAO/pic1L.png";
    int processThreadCount = 4; // 默认总共6线程: 解码1 + 编码1 + 处理4
    if(argc >= 2) {
        videoPath = argv[1];
    }
    if(argc >= 3) {
        refImage = argv[2];
    }
    if(argc >= 4) {
        globalOutVideoPath = argv[3];
    }

    cv::Mat ref_frame = cv::imread(refImage); // 白天参考图像加载 // cv::Mat表示opencv库中的图像矩阵数据类型
    if(ref_frame.empty()) {
        std::cerr << "Could not read the Reference image: " << refImage << std::endl;
        return 1;
    }

    std::cout << "Video path: " << videoPath << std::endl;
    std::cout << "Process thread count: " << processThreadCount << std::endl;
    std::cout << "Ref image: " << refImage << std::endl;
    std::cout << "Out video: " << globalOutVideoPath << std::endl;

    cv::VideoCapture cap(videoPath);
    if(!cap.isOpened()) {
        std::cerr << "Could not open the viedo!" << std::endl;
        return 1;
    }

    frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    input_fps = cap.get(cv::CAP_PROP_FPS);
    if(input_fps <= 0) input_fps = 30.0;

    run.store(true);
    decodeFinished.store(false);
    processingFinished.store(false);
    activeProcessThreads.store(processThreadCount);
    processedFrameCount.store(0, std::memory_order_relaxed);
    nextEncodeIndex.store(1, std::memory_order_relaxed);
    metricsStartTick = cv::getTickCount();

    std::thread decodeThread(videoDecode, std::ref(cap));
    std::thread encodeThread(videoEncode);
    // 这里是创建了两个线程 用于视频的解码和编码

    std::vector<std::thread> pTs;
    for (int i = 0; i < processThreadCount; i++)
        pTs.push_back(std::thread(frameProcess, std::ref(ref_frame)));

    decodeThread.join();
    for (auto& p : pTs)
        p.join();
    encodeThread.join();
    cap.release();
    return 0;
}

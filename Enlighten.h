#ifndef __ENLIGHTEN_H_
#define __ENLIGHTEN_H_

#include <opencv2/opencv.hpp>
#include <opencv2/cudaarithm.hpp>
#include "opencv2/cudawarping.hpp"
#include<opencv2/highgui/highgui.hpp>
#include "opencv2/cudaimgproc.hpp"
#include <vector>


class Enlighten
{
public:
	Enlighten(int fac, double gam, int maxIter, bool cuda = false);

	/*
	-	The function Enlightened the low-light Image to be a Enhanced Image.
	@parma nightImg Night Image need to be processed.
	@parma refImg  Day Image as reference.
	@enlightendImg Enlightened the input nightImg.
	*/
	void applyOnImage(cv::Mat& nightImg, cv::Mat& refImg, cv::Mat& enlightendImg);

	/*
	-	The function Enlightened the low-light Video and show the enhanced through the window named by windowName;
	@parma cap Night Video need to be processed and show
	@parma refImg  Day Image as reference.
	@windowName Display window's name.
	*/
	//void VideoShow(cv::VideoCapture& cap, cv::Mat& refImg, const std::string& windowName);


	void HSVEnhanced_GPU(cv::cuda::GpuMat& g_night_32F, cv::cuda::GpuMat& g_enhanced_32F);

private:
	Enlighten() = default;


	void _LRIREnhanced_GPU(cv::cuda::GpuMat& src, cv::cuda::GpuMat& ref, cv::Mat& dst);
public:
	int gpuAvaliable;


private:

	int Factor; // 缩放系数
	double Gam; // pow操作指数
	int Maxiter; // 最大迭代次数
	bool Cuda; // 使用cuda
	//cv::Mat StepMT; // LRIR
	float StepMT;
	cv::Ptr<cv::cuda::CLAHE> Clahe_GPU; // 一个指向 OpenCV 中 CUDA 版本 CLAHE（对比度限制自适应直方图均衡化）类的智能指针
	cv::Ptr<cv::CLAHE> Clahe_CPU;

	cv::cuda::GpuMat W_gpu; // W矩阵

};


#endif // !__ENLIGHTEN_H_

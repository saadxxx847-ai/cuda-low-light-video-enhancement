#include "Enlighten.h"
namespace cv
{
	using std::vector;
}


Enlighten::Enlighten(int fac, double gam, int maxIter, bool cuda) : Factor(fac), Gam(gam), Maxiter(maxIter), Cuda(cuda)
{
	// 注释
	StepMT = 0.02;
	if (Cuda)
	{
		gpuAvaliable = cv::cuda::getCudaEnabledDeviceCount();  // 检查GPU是否可用
		if (gpuAvaliable > 0)
		{
			// 显卡初始化
			cv::cuda::GpuMat _g_start_test;
			_g_start_test.create(cv::Size(4, 4), CV_8UC3);
			_g_start_test.setTo(cv::Scalar::all(0)); // 初始化为 4x4 的黑色图像

			// CLAHE方法初始化
			Clahe_GPU = cv::cuda::createCLAHE();
			Clahe_GPU->setClipLimit(4.); // 对比度增强限制值设置

			// W矩阵初始化(550*415)

			//float begin = 0.06, finish = 0.04;
			//int number = 550;
			//float interval = (finish - begin) / (number - 1);	
			//cv::Mat W_c(1, number, CV_32F);
			/////*cv::cuda::GpuMat f(1, number, CV_32F);*/
			//for (int i = 0; i < W_c.rows; i++)
			//{
			//	for (int j = 0; j < W_c.cols; j++)
			//	{
			//		W_c.at<double>(i, j) = begin + j * interval;
			//	}
			//}
			//cv::Mat W_r= cv::Mat::ones(416, 1, CV_32F);
			//
			//cv::Mat W = W_r * W_c;

			//cv::Mat three_channels = cv::Mat::zeros(W.rows, W.cols, CV_32FC3);
			//
			//cv::vector<cv::Mat> channels;
			//for (int i = 0; i < 3; i++)
			//{
			//	channels.push_back(W);
			//}
			//merge(channels, three_channels);

			////cv::cuda::GpuMat W_gpu;
			//W_gpu.upload(three_channels);

			//cv::cout << W_gpu << endl;

		}
		else
		{
			throw "cuda is not available!";
		}
	}
	else
	{
		Clahe_CPU = cv::createCLAHE();
		Clahe_CPU->setClipLimit(4.);
	}
}

void Enlighten::applyOnImage(cv::Mat& nightImg, cv::Mat& refImg, cv::Mat& enlightendImg)
{
	if (Cuda)
	{
		// 1、load data
		cv::cuda::GpuMat g_night(nightImg);
		cv::cuda::GpuMat g_ref(refImg);

		// 2、 resize Image  // 对图像进行缩放 主函数中设置缩放系数为2
		cv::cuda::GpuMat g_night_down;
		if (Factor > 1)
			cv::cuda::resize(g_night, g_night_down, cv::Size(g_night.cols / Factor, g_night.rows / Factor));
		else
			g_night_down = g_night;


		// resize W
		/*if(W_gpu.size() != g_night_framed.size())
			cv::cuda::resize(W_gpu, W_gpu, g_night_framed.size());*/

			// 3、change the dataType
		cv::cuda::GpuMat g_night_down_32f, g_ref_32f;
		g_night_down.convertTo(g_night_down_32f, CV_32F);
		g_ref.convertTo(g_ref_32f, CV_32F); // 像素值压缩到 0-1


		// 4、 HSV Enhacnced
		cv::cuda::GpuMat g_night_down_32f_hsv;
		HSVEnhanced_GPU(g_night_down_32f, g_night_down_32f_hsv);

		// 5、 LRIR Enhanced
		_LRIREnhanced_GPU(g_night_down_32f_hsv, g_ref_32f, enlightendImg);
	}
	else
	{
		std::cerr << "CPU ONLY!" << std::endl;
	}
}


void Enlighten::HSVEnhanced_GPU(cv::cuda::GpuMat& g_night_32F, cv::cuda::GpuMat& g_enhanced_32F)
{
	/* BGR -> HSV */
	cv::cuda::GpuMat g_night_32F_hsv;
	cv::cuda::cvtColor(g_night_32F, g_night_32F_hsv, cv::COLOR_BGR2HSV);  // 转HSV空间

	/* Enhanced Channel V */
	std::vector<cv::cuda::GpuMat> g_channels(3);
	cv::cuda::split(g_night_32F_hsv, g_channels);

	// Algebra operation	
	cv::cuda::GpuMat g_temp;
	g_temp.create(g_channels[2].size(), g_channels[2].type());
	cv::cuda::add(g_channels[2], cv::Scalar(1.0), g_temp, cv::Mat(), -1);  // 先整体＋1 防止出现零值影响下一步的对数增强
	cv::cuda::log(g_temp, g_temp); // 对数增强
	cv::cuda::pow(g_temp, Gam, g_temp); // 伽马校正
	cv::cuda::normalize(g_temp, g_channels[2], 0, 255, cv::NORM_MINMAX, CV_8U); // 归一化 并将最后结果存储到原来的亮度通道中

	// CLAHE
	Clahe_GPU->apply(g_channels[2], g_temp);
	g_temp.convertTo(g_channels[2], CV_32F);
	cv::cuda::GpuMat g_night_32f_hsv_clahe;
	cv::cuda::merge(g_channels, g_night_32f_hsv_clahe); // 使用CLAHE增强对比度

	/* HSV -> BGR */
	cv::cuda::cvtColor(g_night_32f_hsv_clahe, g_enhanced_32F, cv::COLOR_HSV2BGR); // 转回RGB
}

void Enlighten::_LRIREnhanced_GPU(cv::cuda::GpuMat& g_night_32F, cv::cuda::GpuMat& g_ref_32F, cv::Mat& dst)
{

	if (g_ref_32F.size() != g_night_32F.size())
		cv::cuda::resize(g_ref_32F, g_ref_32F, g_night_32F.size());


	cv::cuda::GpuMat g_subres, g_mulRes, g_res01;
	for (int i = 0; i < Maxiter; ++i) // 基于迭代的增强算法 每次减去与参考图像的插值
	{
		cv::cuda::subtract(g_night_32F, g_ref_32F, g_subres);
		cv::cuda::multiply(g_subres, cv::Scalar::all(StepMT), g_mulRes);
		cv::cuda::subtract(g_night_32F, g_mulRes, g_night_32F);
	}


	std::vector<cv::cuda::GpuMat> channels;
	cv::cuda::split(g_night_32F, channels);
	for (int i = 0; i < 3; ++i)
		cv::cuda::normalize(channels[i], channels[i], 0, 255, cv::NORM_MINMAX, CV_8U);
	cv::cuda::GpuMat g_night_32F_LRIR;
	cv::cuda::merge(channels, g_night_32F_LRIR);

	cv::cuda::GpuMat g_night_32F_LRIR_UP;
	cv::cuda::resize(g_night_32F_LRIR, g_night_32F_LRIR_UP,
		cv::Size(g_night_32F_LRIR.cols * Factor, g_night_32F_LRIR.rows * Factor)); // 变回原来的尺寸

	g_night_32F_LRIR_UP.download(dst);// 下载
}

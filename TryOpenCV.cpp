

#include "stdafx.h"
#include "opencv2/imgproc/imgproc.hpp"
#include <opencv2/opencv.hpp>
#include "opencv2/highgui/highgui.hpp"
#include <stdio.h>
#include <string>
#include <iostream>



using namespace std;
using namespace cv;

Mat img, dst, tmp;
string window_name = "OCR Process Image";

Mat inImg;
Mat outImg;
Mat outImg_gray;
Mat outImg_binarized;
Mat textRoi;


//=================================================================================================
//==================================== HELPER FUNTIONS ============================================
//=================================================================================================

float dist2(Point p1, Point p2)
{
	return float((p1.x - p2.x)*(p1.x - p2.x) + (p1.y - p2.y)*(p1.y - p2.y));
}

float lineLength(Vec4i a)
{
	int x1 = a[0], y1 = a[1], x2 = a[2], y2 = a[3];
	return sqrt(float((x1 - x2)*(x1 - x2)) + float((y1 - y2)*(y1 - y2)));
}

void findIntersectionLoc(Vec4i a, Point p, float& lambda, float& dist)
{
	int x1 = a[0], y1 = a[1], x2 = a[2], y2 = a[3];
	lambda = float((p.x - x1)*(x2 - x1) + (p.y - y1)*(y2 - y1)) / float((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
	float dx = x1 + lambda * (x2 - x1) - p.x;
	float dy = y1 + lambda * (y2 - y1) - p.y;
	dist = sqrt(dx*dx + dy * dy);
}

void sortPointsClockwise(Point a[])
{
	Point b[4];

	Point ctr = (a[0] + a[1] + a[2] + a[3]);
	ctr.x /= 4;
	ctr.y /= 4;
	b[0] = a[0] - ctr;
	b[1] = a[1] - ctr;
	b[2] = a[2] - ctr;
	b[3] = a[3] - ctr;

	for (int i = 0; i<4; i++)
	{
		if (b[i].x < 0)
		{
			if (b[i].y < 0)
				a[0] = b[i] + ctr;
			else
				a[3] = b[i] + ctr;
		}
		else
		{
			if (b[i].y < 0)
				a[1] = b[i] + ctr;
			else
				a[2] = b[i] + ctr;
		}
	}

}

Point computeIntersect(cv::Vec4i a, cv::Vec4i b, cv::Rect ROI)
{
	int x1 = a[0], y1 = a[1], x2 = a[2], y2 = a[3];
	int x3 = b[0], y3 = b[1], x4 = b[2], y4 = b[3];

	Point p1 = Point(x1, y1);
	Point p2 = Point(x2, y2);
	Point p3 = Point(x3, y3);
	Point p4 = Point(x4, y4);

	if (!ROI.contains(p1) || !ROI.contains(p2)
		|| !ROI.contains(p3) || !ROI.contains(p4))
		return Point(-1, -1);

	Point vec1 = p1 - p2;
	Point vec2 = p3 - p4;

	float vec1_norm2 = vec1.x*vec1.x + vec1.y*vec1.y;
	float vec2_norm2 = vec2.x*vec2.x + vec2.y*vec2.y;
	float cosTheta = (vec1.dot(vec2)) / sqrt(vec1_norm2*vec2_norm2);

	float den = ((float)(x1 - x2) * (y3 - y4)) - ((y1 - y2) * (x3 - x4));
	if (den != 0)
	{
		cv::Point2f pt;
		pt.x = ((x1*y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3*y4 - y3 * x4)) / den;
		pt.y = ((x1*y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3*y4 - y3 * x4)) / den;

		if (!ROI.contains(pt))
			return Point(-1, -1);

		// no-confidence metric
		float d1 = MIN(dist2(p1, pt), dist2(p2, pt)) / vec1_norm2;
		float d2 = MIN(dist2(p3, pt), dist2(p4, pt)) / vec2_norm2;

		float no_confidence_metric = MAX(sqrt(d1), sqrt(d2));

		if (no_confidence_metric < 0.5 && cosTheta < 0.707)
			return Point(int(pt.x + 0.5), int(pt.y + 0.5));
	}

	return cv::Point(-1, -1);
}


float getArea(Point arr[])
{
	Point diag1 = arr[0] - arr[2];
	Point diag2 = arr[1] - arr[3];

	return 0.5*(diag1.cross(diag2));
}


bool isCloseBy(Point p1, Point p2)
{
	int D = 10;
	return (abs(p1.x - p2.x) <= D && abs(p1.y - p2.y) <= D);
}

Rect getContourBoundingBox(vector<Point> arr)
{
	cout << "\nContour of length = " << arr.size();
	int x_min = INT_MAX;
	int y_min = INT_MAX;
	int x_max = -INT_MAX;
	int y_max = -INT_MAX;
	for (int i = 0; i<arr.size(); i++)
	{
		Point p = arr[i];

		if (p.x < x_min)
			x_min = p.x;
		if (p.y < y_min)
			y_min = p.y;
		if (p.x > x_max)
			x_max = p.x;
		if (p.y > y_max)
			y_max = p.y;
	}

	Rect ret;
	ret.x = x_min;
	ret.y = y_min;
	ret.width = x_max - x_min + 1;
	ret.height = y_max - y_min + 1;

	return ret;
}


int main(int argc, char** argv)
{

	string imageName("D:/img4.jpg"); // by default
	if (argc > 1)
	{
		imageName = argv[1];
	}
	Mat image;
	inImg = imread(imageName.c_str(), IMREAD_COLOR);

	//inImg = imread("D:\img.jpg");
	img = inImg.clone();
	outImg = Mat(inImg.size(), CV_8UC3);
	inImg.copyTo(outImg);
	/// Test image - Make sure it s divisible by 2^{n}

	if (!img.data)
	{
		printf(" No data! -- Exiting the program \n");
		return -1;
	}

	/// Create window
	namedWindow(window_name, CV_WINDOW_AUTOSIZE);

	Mat img_fullRes = img.clone();

	// downsize the image for processing
	//pyrDown(img, img);

	// -------------- find edges --------------- //
	// convert to grayscale
	Mat imgGray;
	cvtColor(img, imgGray, CV_BGR2GRAY);

	// get the edge map
	Mat detectedEdges = imgGray.clone();
	Canny(detectedEdges, detectedEdges, 20, 80, 3);
	//imshow("Edges_orig", detectedEdges);

	// ---- find the corners(goc) of the card ----- //
	cv::dilate(detectedEdges, detectedEdges, Mat::ones(3, 3, CV_8UC1));// gian no
	imshow("Edges", detectedEdges);
	Mat cdst = img.clone();
	vector<Vec4i> lines;
	HoughLinesP(detectedEdges, lines, 1, CV_PI / 180, 50, 50, 3); //detect lines in an image.  output the extremes of the detected lines (x_{0}, y_{0}, x_{1}, y_{1})
	for (size_t i = 0; i < lines.size(); i++) // Draw the lines
	{
		Vec4i l = lines[i];
		line(cdst, Point(l[0], l[1]), Point(l[2], l[3]), Scalar(0, 0, 255), 1);
	}

	imshow("HoughLinesP", cdst);

	// -------------- find points of intersection(giao) --------------- //
	Rect imgROI;
	int ext = 10;
	imgROI.x = ext;
	imgROI.y = ext;
	imgROI.width = img.size().width - ext;
	imgROI.height = img.size().height - ext;

	int N = lines.size();
	Point** poi = new Point*[N];
	for (int i = 0; i<N; i++)
		poi[i] = new Point[N];
	vector<Point> poiList;
	for (int i = 0; i<N; i++)
	{
		poi[i][i] = Point(-1, -1);
		Vec4i line1 = lines[i];
		for (int j = i + 1; j<N; j++)
		{
			Vec4i line2 = lines[j];
			Point p = computeIntersect(line1, line2, imgROI);

			if (p.x != -1)
			{
				line(cdst, p - Point(2, 0), p + Point(2, 0), Scalar(0, 255, 0));
				line(cdst, p - Point(0, 2), p + Point(0, 2), Scalar(0, 255, 0));
				poiList.push_back(p);
			}

			poi[i][j] = p;
			poi[j][i] = p;
		}
	}

	imshow("all_intersections", cdst);

	if (poiList.size() == 0)
	{
		outImg = inImg.clone();
		circle(outImg, Point(100, 100), 50, Scalar(255, 0, 0), -1);
		return 0;
	}

	cout << "\nBefore CVX Hull: " << poiList.size();
	vector<Point> poiList2;
	convexHull(poiList, poiList2, false, true);

	cout << "\nAfter CVX Hull: " << poiList2.size();
	/*for (int i = 0; i<poiList.size(); i++)
	{
	Point p = hull[i];
	circle(cdst, p, 3, Scalar(255, 0, 0), 2);
	}
	imshow("CVX hull points", cdst); */

	// -------------- Evaluate all possible quadrilaterals --------------- // danh gia, tim 4 goc
	Point cardCorners[4];
	float metric_max = 0;
	int Npoi = poiList2.size();
	for (int p1 = 0; p1<Npoi; p1++)
	{
		Point pts[4];
		pts[0] = poiList2[p1];

		for (int p2 = p1 + 1; p2<Npoi; p2++)
		{
			pts[1] = poiList2[p2];
			if (isCloseBy(pts[1], pts[0]))
				continue;

			for (int p3 = p2 + 1; p3<Npoi; p3++)
			{
				pts[2] = poiList2[p3];
				if (isCloseBy(pts[2], pts[1]) || isCloseBy(pts[2], pts[0]))
					continue;


				for (int p4 = p3 + 1; p4<Npoi; p4++)
				{
					pts[3] = poiList2[p4];
					if (isCloseBy(pts[3], pts[0]) || isCloseBy(pts[3], pts[1])
						|| isCloseBy(pts[3], pts[2]))
						continue;


					// get the metrics
					float area = getArea(pts);

					Point a = pts[0] - pts[1];
					Point b = pts[1] - pts[2];
					Point c = pts[2] - pts[3];
					Point d = pts[3] - pts[0];
					float oppLenDiff = abs(a.dot(a) - c.dot(c)) + abs(b.dot(b) - d.dot(d));

					float metric = area - 0.35*oppLenDiff;
					if (metric > metric_max)
					{
						metric_max = metric;
						cardCorners[0] = pts[0];
						cardCorners[1] = pts[1];
						cardCorners[2] = pts[2];
						cardCorners[3] = pts[3];
					}

				}
			}
		}
	}

	// find the corners corresponding to the 4 corners of the physical card
	sortPointsClockwise(cardCorners);// sort theo chieu dong ho

	cdst = img.clone();
	circle(cdst, cardCorners[0], 5, Scalar(255, 0, 0), -1);
	circle(cdst, cardCorners[1], 5, Scalar(0, 255, 0), -1);
	circle(cdst, cardCorners[2], 5, Scalar(0, 0, 255), -1);
	circle(cdst, cardCorners[3], 5, Scalar(0, 0, 0), -1);
	line(cdst, cardCorners[0], cardCorners[1], Scalar(0, 0, 255), 2);
	line(cdst, cardCorners[1], cardCorners[2], Scalar(0, 0, 255), 2);
	line(cdst, cardCorners[2], cardCorners[3], Scalar(0, 0, 255), 2);
	line(cdst, cardCorners[3], cardCorners[0], Scalar(0, 0, 255), 2);
	imshow("Corners", cdst);

	// --------------- Calculate Homography --------------- //
	vector<Point2f> srcPts(4);
	srcPts[0] = cardCorners[0];//*2 maybe voi anh kich thuoc to
	srcPts[1] = cardCorners[1];//*2
	srcPts[2] = cardCorners[2];//*2
	srcPts[3] = cardCorners[3];//*2

	vector<Point2f> dstPts(4);
	Size outImgSize(1400, 800);

	dstPts[0] = Point2f(0, 0);
	dstPts[1] = Point2f(outImgSize.width - 1, 0);
	dstPts[2] = Point2f(outImgSize.width - 1, outImgSize.height - 1);
	dstPts[3] = Point2f(0, outImgSize.height - 1);

	Mat Homography = findHomography(srcPts, dstPts);

	// Apply Homography
	warpPerspective(img_fullRes, outImg, Homography, outImgSize, INTER_CUBIC);

	// =============================================
	// ============== find text boxes ==============
	// =============================================

	cvtColor(outImg, outImg_gray, CV_BGR2GRAY);

	// calculate the local variances of the grayscale image
	Mat t_mean, t_mean_2;
	Mat grayF;
	outImg_gray.convertTo(grayF, CV_32F);
	int winSize = 35;
	blur(grayF, t_mean, Size(winSize, winSize));
	blur(grayF.mul(grayF), t_mean_2, Size(winSize, winSize));
	Mat varMat = t_mean_2 - t_mean.mul(t_mean);
	varMat.convertTo(varMat, CV_8U);

	// threshold the high variance regions
	Mat varMatRegions = varMat > 100;

	// crop the borders of the image (often noisy due to rectification imperfections)
	Rect CropROI;
	int CropPxls = 50;
	CropROI.x = CropPxls; CropROI.y = CropPxls; CropROI.width = varMat.cols - 2 * CropPxls; CropROI.height = varMat.rows - 2 * CropPxls;
	varMatRegions = varMatRegions(CropROI);

	// find the bounding boxes of high variance regions
	vector<vector<Point> > contours;
	cv::findContours(varMatRegions, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
	cout << "\nContours found = " << contours.size() << "\n"; cout.flush();
	Rect *ROI = new Rect[contours.size()];
	vector<Rect> goodROI;
	for (int i = 0; i<contours.size(); i++)
	{
		vector<Point> ctr = contours[i];
		ROI[i] = getContourBoundingBox(ctr);
		ROI[i].x += CropPxls;
		ROI[i].y += CropPxls;
		if (ROI[i].area() < 2000
			|| ROI[i].width / ROI[i].height > 10
			|| ROI[i].area() > varMatRegions.cols*varMatRegions.rows / 2
			)
			continue;

		goodROI.push_back(ROI[i]);
	}


	// ======================================
	// ========== binarization ==============
	// ======================================

	// global threshold to determine the type of the card
	Mat global_temp_thresh;
	threshold(outImg_gray, global_temp_thresh, 0, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
	bool whiteBkg;
	if (sum(global_temp_thresh).val[0] > global_temp_thresh.cols*global_temp_thresh.rows * 255 / 2)
	{
		// its black text on white background
		whiteBkg = true;
		outImg_binarized = Mat::zeros(varMat.size(), CV_8UC1);
	}
	else
	{
		// its white text on dark background
		whiteBkg = false;
		outImg_binarized = Mat::zeros(varMat.size(), CV_8UC1);
	}

	// locally adaptive threshold for each of the bounding boxes
	for (int i = 0; i<goodROI.size(); i++)
	{
		Rect boxRoi = goodROI[i];

		if (whiteBkg)
			threshold(outImg_gray(boxRoi), outImg_binarized(boxRoi), 0, 255, CV_THRESH_BINARY_INV | CV_THRESH_OTSU);
		else
			threshold(outImg_gray(boxRoi), outImg_binarized(boxRoi), 0, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
	}

	Mat bw_cpy = outImg_binarized.clone();
	cvtColor(bw_cpy, bw_cpy, CV_GRAY2BGR);

	textRoi = Mat(goodROI.size(), 4, CV_32SC1);
	for (int i = 0; i<goodROI.size(); i++)
	{
		Rect roi = goodROI[i];
		textRoi.at<int>(i, 0) = roi.x;
		textRoi.at<int>(i, 1) = roi.y;
		textRoi.at<int>(i, 2) = roi.width;
		textRoi.at<int>(i, 3) = roi.height;

		cv::rectangle(bw_cpy, roi, Scalar(0, 0, 255), 3);
	}

	outImg_binarized = 255 - outImg_binarized;
	cvtColor(outImg_binarized, outImg_binarized, CV_GRAY2BGR);

	imshow("outImg", outImg);
	imshow("outImgGray", outImg_gray);
	imshow("varMat", varMat);
	imshow("bw", outImg_binarized);
	imshow("outImg", outImg);
	waitKey(0);

	return 0;
}
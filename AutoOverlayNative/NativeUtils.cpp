#define _USE_MATH_DEFINES

#include "Stdafx.h"
#include <iostream>
#include <math.h>

namespace AutoOverlay {

	const double EPSILON = std::numeric_limits<double>::epsilon();

	double NativeUtils::SquaredDifferenceSum(
		const unsigned char *src, int srcStride,
		const unsigned char *over, int overStride,
		int width, int height)
	{
		__int64 sum = 0;
		for (int row = 0; row < height; ++row)
		{
			for (int col = 0; col < width; ++col)
			{
				int diff = src[col] - over[col];
				int square = diff * diff;
				sum += square;
			}
			src += srcStride;
			over += overStride;
		}
		return (double)sum / (width*height);
	}

	double NativeUtils::SquaredDifferenceSumMasked(
		const unsigned char *src, int srcStride,
		const unsigned char *srcMask, int srcMaskStride,
		const unsigned char *over, int overStride,
		const unsigned char *overMask, int overMaskStride,
		int width, int height)
	{
		__int64 sum = 0;
		int pixelCount = width * height;
		for (int row = 0; row < height; ++row)
		{
			for (int col = 0; col < width; ++col)
			{
				if ((srcMask == nullptr || srcMask[col] > 0) 
					&& (overMask == nullptr || overMask[col] > 0)) 
				{
					int diff = src[col] - over[col];
					int square = diff * diff;
					sum += square;
				} 
				else
				{
					pixelCount--;
				}
			}
			src += srcStride;
			over += overStride;
			if (srcMask != nullptr)
				srcMask += srcMaskStride;
			if (overMask != nullptr)
				overMask += overMaskStride;
		}
		return (double)sum / pixelCount;
	}

	array<int>^ NativeUtils::Histogram8bit(
		IntPtr image, int stride, int height, int rowSize, int pixelSize)
	{
		array<int>^ histogram = gcnew array<int>(256);
		pin_ptr<int> first = &histogram[0];
		int* hist = first;
		unsigned char* data = reinterpret_cast<unsigned char*>(image.ToPointer());
		for (int y = 0; y < height; y++, data += stride)
			for (int x = 0; x < rowSize; x += pixelSize)
				hist[data[x]]++;
		return histogram;
	}

	array<int>^ NativeUtils::Histogram8bitMasked(
		int width, int height,
		IntPtr image, int imageStride, int imagePixelSize,
		IntPtr mask, int maskStride, int maskPixelSize)
	{		
		array<int>^ histogram = gcnew array<int>(256);
		pin_ptr<int> first = &histogram[0];
		int* hist = first;
		int imageRowSize = width * imagePixelSize;
		unsigned char* data = reinterpret_cast<unsigned char*>(image.ToPointer());
		unsigned char* maskData = reinterpret_cast<unsigned char*>(mask.ToPointer());
		for (int y = 0; y < height; y++, data += imageStride, maskData += maskStride)
			for (int x = 0, xMask = 0; x < imageRowSize; x += imagePixelSize, xMask += maskPixelSize)
				if (maskData[xMask] > 0)
					hist[data[x]]++;
		return histogram;
	}

	void NativeUtils::ApplyColorMap(IntPtr read, IntPtr write, int height, int stride, int rowSize, int pixelSize,
		array<unsigned char>^ fixedColors, array<unsigned char, 2>^ dynamicColors, array<double, 2>^ dynamicWeights)
	{
		unsigned char* readData = reinterpret_cast<unsigned char*>(read.ToPointer());
		unsigned char* writeData = reinterpret_cast<unsigned char*>(write.ToPointer());
		pin_ptr<unsigned char> firstFixedColor = &fixedColors[0];
		unsigned char* fixedColor = firstFixedColor;
		pin_ptr<unsigned char> firstDynamicColor = &dynamicColors[0, 0];
		unsigned char* dynamicColor = firstDynamicColor;
		pin_ptr<double> firstDynamicWeight = &dynamicWeights[0,0];
		double* dynamicWeight = firstDynamicWeight;		
		XorshiftRandom^ rand = gcnew XorshiftRandom();
		for (int y = 0; y < height; y++, readData += stride, writeData += stride)
			for (int x = 0; x < rowSize; x += pixelSize)
			{
				unsigned char oldColor = readData[x];
				unsigned char newColor = fixedColor[oldColor];
				if (newColor == 0)
				{
					double weight = rand->NextDouble();
					for (int offset = oldColor << 8;; offset++)
						if ((weight -= dynamicWeight[offset]) < EPSILON)
						{
							newColor = dynamicColor[offset];
							break;
						}
				}
				writeData[x] = newColor;
			}
	}

	void BilinearRotate1(
		IntPtr srcImage, int srcWidth, int srcHeight, int srcStride,
		IntPtr dstImage, int dstWidth, int dstHeight, int dstStride,
		double angle)
	{
		unsigned char* src = reinterpret_cast<unsigned char*>(srcImage.ToPointer());
		unsigned char* dst = reinterpret_cast<unsigned char*>(dstImage.ToPointer());

		double oldXradius = (srcWidth - 1) / 2.0;
		double oldYradius = (srcHeight - 1) / 2.0;
		double newXradius = (dstWidth - 1) / 2.0;
		double newYradius = (dstHeight - 1) / 2.0;
		int dstOffset = dstStride - dstWidth;

		double angleRad = -angle * M_PI / 180;
		double angleCos = cos(angleRad);
		double angleSin = sin(angleRad);

		int ymax = srcHeight - 1;
		int xmax = srcWidth - 1;

		double cy = -newYradius;
		for (int y = 0; y < dstHeight; y++, cy++, dst += dstStride)
		{
			// do some pre-calculations of source points' coordinates
			// (calculate the part which depends on y-loop, but does not
			// depend on x-loop)
			double tx = angleSin * cy + oldXradius;
			double ty = angleCos * cy + oldYradius;

			double cx = -newXradius;
			for (int x = 0; x < dstWidth; x++, cx++)
			{
				// coordinates of source point
				double ox = tx + angleCos * cx;
				double oy = ty - angleSin * cx;

				// top-left coordinate
				int ox1 = (int)ox;
				int oy1 = (int)oy;

				// validate source pixel's coordinates
				if (ox1 >= 0 && oy1 >= 0 && ox1 < srcWidth && oy1 < srcHeight)
				{
					// bottom-right coordinate
					int ox2 = ox1 == xmax ? ox1 : ox1 + 1;
					int oy2 = oy1 == ymax ? oy1 : oy1 + 1;

					double dx1 = max(0.0, ox - ox1);
					double dx2 = 1.0 - dx1;

					double dy1 = max(0.0, oy - oy1);
					double dy2 = 1.0 - dy1;

					// get four points
					unsigned char* p = src + oy1 * srcStride;
					unsigned char p1 = p[ox1];
					unsigned char p2 = p[ox2];

					p = src + oy2 * srcStride;
					unsigned char p3 = p[ox1];
					unsigned char p4 = p[ox2];

					dst[x] = (unsigned char)(dy2 * (dx2 * p1 + dx1 * p2) + dy1 * (dx2 * p3 + dx1 * p4));
				}
			}
		}
	}

	void NativeUtils::BilinearRotate(
		IntPtr srcImage, int srcWidth, int srcHeight, int srcStride,
		IntPtr dstImage, int dstWidth, int dstHeight, int dstStride,
		double angle, int pixelSize)
	{
		if (pixelSize == 1)
		{
			BilinearRotate1(
				srcImage, srcWidth, srcHeight, srcStride,
				dstImage, dstWidth, dstHeight, dstStride,
				angle);
			return;
		}
		unsigned char* src = reinterpret_cast<unsigned char*>(srcImage.ToPointer());
		unsigned char* dst = reinterpret_cast<unsigned char*>(dstImage.ToPointer());

		double oldXradius = (srcWidth - 1) / 2.0;
		double oldYradius = (srcHeight - 1) / 2.0;
		double newXradius = (dstWidth - 1) / 2.0;
		double newYradius = (dstHeight - 1) / 2.0;
		int dstOffset = dstStride - dstWidth * pixelSize;

		double angleRad = -angle * M_PI / 180;
		double angleCos = cos(angleRad);
		double angleSin = sin(angleRad);

		int ymax = srcHeight - 1;
		int xmax = srcWidth - 1;

		double cy = -newYradius;
		for (int y = 0; y < dstHeight; y++, cy++)
		{
			// do some pre-calculations of source points' coordinates
			// (calculate the part which depends on y-loop, but does not
			// depend on x-loop)
			double tx = angleSin * cy + oldXradius;
			double ty = angleCos * cy + oldYradius;

			double cx = -newXradius;
			for (int x = 0; x < dstWidth; x++, dst += pixelSize, cx++)
			{
				// coordinates of source point
				double ox = tx + angleCos * cx;
				double oy = ty - angleSin * cx;

				// top-left coordinate
				int ox1 = (int)ox;
				int oy1 = (int)oy;

				// validate source pixel's coordinates
				if (ox1 >= 0 && oy1 >= 0 && ox1 < srcWidth && oy1 < srcHeight)
				{
					// bottom-right coordinate
					int ox2 = ox1 == xmax ? ox1 : ox1 + 1;
					int oy2 = oy1 == ymax ? oy1 : oy1 + 1;

					double dx1 = max(0.0, ox - ox1);
					double dx2 = 1.0 - dx1;

					double dy1 = max(0.0, oy - oy1);
					double dy2 = 1.0 - dy1;

					// get four points
					unsigned char* p1 = src + oy1 * srcStride;
					unsigned char* p2 = p1;
					p1 += ox1 * pixelSize;
					p2 += ox2 * pixelSize;

					unsigned char* p3 = src + oy2 * srcStride;
					unsigned char* p4 = p3;
					p3 += ox1 * pixelSize;
					p4 += ox2 * pixelSize;

					// interpolate using 4 points
					for (int z = 0; z < pixelSize; z++)
					{
						dst[z] = (unsigned char)
							(dy2 * (dx2 * p1[z] + dx1 * p2[z]) +
								dy1 * (dx2 * p3[z] + dx1 * p4[z]));
					}
				}
			}
			dst += dstOffset;
		}
	}
}
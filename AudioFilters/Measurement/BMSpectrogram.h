//
//  BMSpectrogram.h
//  AudioFiltersXcodeProject
//
//  Created by hans anderson on 5/15/20.
//  Copyright © 2020 BlueMangoo. All rights reserved.
//

#ifndef BMSpectrogram_h
#define BMSpectrogram_h

#include <stdio.h>
#include "BMSpectrum.h"
#include <simd/simd.h>

typedef struct BMSpectrogram {
    BMSpectrum spectrum;
    float *b1, *b2, *b3, *b6;
    size_t *b4, *b5;
    float prevMinF, prevMaxF, sampleRate, pixelBinParityFrequency;
    size_t prevImageHeight, prevFFTSize, maxImageHeight, maxFFTSize, fftBinInterpolationPadding, upsampledPixels;
} BMSpectrogram;



typedef simd_float3 BMHSBPixel;
typedef struct BMRGBPixel {
    uint8_t r,g,b;
} BMRGBPixel;



/*!
 *BMSpectrogram_init
 *
 * Once initialised, this spectrogram object can generate spectrograms with any
 * FFTSize <= maxFFTSize and any image height <= maxImageHeight.
 *
 * @param This pointer to a spectrogram struct
 * @param maxFFTSize the process function may use any fft size <= maxFFTSize
 * @param maxImageHeight the height of the output image is not linked to the fft size. This setting exists so that we can allocate memory for internal buffers. the process function may use any image height <= maxImageHeight
 */
void BMSpectrogram_init(BMSpectrogram *This,
                        size_t maxFFTSize,
                        size_t maxImageHeight,
                        float sampleRate);

void BMSpectrogram_free(BMSpectrogram *This);


/*!
 * returns the padding, which is the number of extra pixels that must be
 * included in the input audio array before startSampleIndex
 */
float BMSpectrogram_getPaddingLeft(size_t fftSize);


/*!
 * returns the padding, which is the number of extra pixels that must be
 * included in the input audio array after endSampleIndex
 */
float BMSpectrogram_getPaddingRight(size_t fftSize);



/*!
 *BMSpectrogram_process
 *
 * This function takes an audio array as input and converts it to a 2d image in
 * RGB colour format.
 *
 * It requires some additional samples to be available in the input array beyond
 * the region you actually plan to plot. Call BMSpectrogram_getPaddingLeft(...)
 * and BMSpectrogram_getPaddingRight(...) to find out how many extra samples you
 * need to provide.
 *
 * @param This pointer to an initialised struct
 * @param inputAudio input array with length = inputLength
 * @param inputLength length of input audio array, including padding
 * @param startSampleIndex first sample of audio in inputAudio that you want to draw on the screen
 * @param endSampleIndex last sample of audio in inputAudio that you want to draw on the screen
 * @param fftSize length of fft. must be an integer power of two. each fft output represents one row of pixels in the image. overlap and stride are computed from fftSize and pixelWidth
 * @param imageOutput an array of RGBA pixels with 32 bits per pixel, in column major order, having height = pixelHeight and width = pixelWidth
 * @param pixelWidth width of image output in pixels. one pixel for each fft output
 * @param pixelHeight height of image outptu in pixels. the output is in bark scale frequency, interpolated fromt the FFT output so the pixelHeight does not correspond to the fft length or the frequency resolution.
 */
void BMSpectrogram_process(BMSpectrogram *This,
                           const float* inputAudio,
                           SInt32 inputLength,
                           SInt32 startSampleIndex,
                           SInt32 endSampleIndex,
                           SInt32 fftSize,
                           uint8_t *imageOutput,
                           SInt32 pixelWidth,
                           SInt32 pixelHeight,
                           float minFrequency,
                           float maxFrequency);


size_t BMSpectrogram_GetFFTSizeFor(BMSpectrogram *This, size_t pixelWidth, size_t sampleWidth);

#endif /* BMSpectrogram_h */

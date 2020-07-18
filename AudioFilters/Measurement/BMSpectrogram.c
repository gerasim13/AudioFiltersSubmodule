//
//  BMSpectrogram.c
//  AudioFiltersXcodeProject
//
//  Created by hans anderson on 5/15/20.
//  Copyright © 2020 BlueMangoo. All rights reserved.
//

#include "BMSpectrogram.h"
#include "BMIntegerMath.h"
#include "Constants.h"

#define BMSG_BYTES_PER_PIXEL 4
#define SG_MIN(a,b) (((a)<(b))?(a):(b))
#define SG_MAX(a,b) (((a)>(b))?(a):(b))

void BMSpectrogram_init(BMSpectrogram *This,
                        size_t maxFFTSize,
                        size_t maxImageHeight,
                        float sampleRate){
    
    This->sampleRate = sampleRate;
    This->prevMinF = This->prevMaxF = 0.0f;
    This->prevImageHeight = This->prevFFTSize = 0;
    
    BMSpectrum_initWithLength(&This->spectrum, maxFFTSize);
    This->maxFFTSize = maxFFTSize;
    This->maxImageHeight = maxImageHeight;
    
    // we need some extra samples at the end of the fftBin array because the
    // interpolating function that converts from linear scale to bark scale
    // reads beyond the interpolation index
    This->fftBinInterpolationPadding = 2;
    
    size_t maxFFTOutput = 1 + maxFFTSize/2;
    This->b1 = malloc(sizeof(float)*(maxFFTOutput+This->fftBinInterpolationPadding));
    This->b2 = malloc(sizeof(float)*maxImageHeight);
    This->b3 = malloc(sizeof(float)*maxImageHeight);
    This->b4 = malloc(sizeof(size_t)*maxImageHeight);
    This->b5 = malloc(sizeof(size_t)*maxImageHeight);
}

void BMSpectrogram_free(BMSpectrogram *This){
    free(This->b1);
    free(This->b2);
    free(This->b3);
    free(This->b4);
    free(This->b5);
    This->b1 = NULL;
    This->b2 = NULL;
    This->b3 = NULL;
    This->b4 = NULL;
    This->b5 = NULL;
    
    BMSpectrum_free(&This->spectrum);
}

float hzToBark(float hz){
    // hzToBark[hz_] := 6 ArcSinh[hz/600]
    return 6.0f * asinhf(hz / 600.0f);
}


//void hzToBarkVDSP(const float *hz, float *bark, size_t length){
//
//    // hz/600
//    float div600 = 1.0 / 600.0;
//    vDSP_vsmul(hz, 1, &div600, bark, 1, length);
//
//    // asinf
//    int length_i = (int)length;
//    vvasinf(bark, bark, &length_i);
//
//    // * 6.0
//    float six = 6.0f;
//    vDSP_vsmul(bark, 1, &six, bark, 1, length);
//}



float barkToHz(float bark){
    // barkToHz[bk_] := 600 Sinh[bk/6]
    return 600.0f * sinhf(bark / 6.0f);
}



float hzToFFTBin(float hz, float fftSize, float sampleRate){
    // fftBinZeroDC[f_, fftLength_, sampleRate_] := f/(sampleRate/fftLength)
    return hz * (fftSize / sampleRate);
}


// how many fft bins are represented by a single pixel at freqHz?
float fftBinsPerPixel(float freqHz,
                      float fftSize,
                      float pixelHeight,
                      float minFrequency,
                      float maxFrequency,
                      float sampleRate){
    
    // what bark frequency range does 1 pixel represent?
    float windowHeightInBarks = hzToBark(maxFrequency) - hzToBark(minFrequency);
    float pixelHeightInBarks = windowHeightInBarks / pixelHeight;
    
    // what frequency is one pixel above freqHz?
    float upperPixelFreq = barkToHz(hzToBark(freqHz)+pixelHeightInBarks);
    
    // return the height of one pixel in FFT bins
    return hzToFFTBin(upperPixelFreq, fftSize, sampleRate) - hzToFFTBin(freqHz, fftSize, sampleRate);
}



// returns the frequency at which one pixel is equal to one FFT bin.
float pixelBinParityFrequency(float fftSize, float pixelHeight,
                              float minFrequency, float maxFrequency,
                              float sampleRate){
    // initial guess
    float f = sampleRate / 4.0;
    
    // numerical search for a value of f where binsPerPixel is near 1.0
    while(true){
        // find out how many bins per pixel at frequency f
        float binsPerPixel = fftBinsPerPixel(f, fftSize, pixelHeight, minFrequency, maxFrequency, sampleRate);
        
        // if binsPerPixel < 1, increase f. else decrease f
        f /= (binsPerPixel / 3.0f);
        
        // if binsPerPixel is close to 1, return
        if (fabsf(binsPerPixel - 3.0f) < 0.001) return f;
        
        // if f is below half of minFrequency, stop
        if (f < minFrequency * 0.5f) return minFrequency;
        
        // if f is above 2x maxFrequency, stop
        if (f > maxFrequency * 2.0f) return maxFrequency;
    }
    
    // satisfy the compiler
    return f;
}


void BMSpectrogram_fftBinsToBarkScale(BMSpectrogram *This,
                                      const float* fftBins,
                                      float *output,
                                      size_t fftSize,
                                      size_t outputLength,
                                      float minFrequency,
                                      float maxFrequency){
    // rename buffer 3 and 4 for readablility
    float* interpolatedIndices = This->b3;
    size_t* integerIndices = This->b4;
    size_t* binsPerPixel = This->b5;
    
    // if the settings have changed since last time we did this, recompute
    // the floating point array indices for interpolated reading of bark scale
    // frequency spectrum
    if(minFrequency != This->prevMinF ||
       maxFrequency != This->prevMaxF ||
       outputLength != This->prevImageHeight ||
       fftSize != This->prevFFTSize){
        This->prevMinF = minFrequency;
        This->prevMaxF = maxFrequency;
        This->prevImageHeight = outputLength;
        This->prevFFTSize = fftSize;
        
        // find the min and max frequency in barks
        float minBark = hzToBark(minFrequency);
        float maxBark = hzToBark(maxFrequency);
        
        
        for(size_t i=0; i<outputLength; i++){
            // create evenly spaced values in bark scale
            float zeroToOne = (float)i / (float)(outputLength-1);
            float bark = minBark + (maxBark-minBark)*zeroToOne;
            
            // convert to Hz
            float hz = barkToHz(bark);
            
            // convert to FFT bin index (floating point interpolated)
            interpolatedIndices[i] = hzToFFTBin(hz, fftSize, This->sampleRate);
            
            // convert the floating point index to an integer index
            integerIndices[i] = (size_t)interpolatedIndices[i];
            
            // calculate bins per pixel at this index
            binsPerPixel[i] = round(1.0f * fftBinsPerPixel(hz, fftSize, outputLength, minFrequency, maxFrequency, This->sampleRate));
        }
        
        // find the frequency at which 1 fft bin = 1 screen pixel
        This->pixelBinParityFrequency = pixelBinParityFrequency(fftSize, outputLength, minFrequency, maxFrequency, This->sampleRate);
        
        // find the number of pixels we can interpolate by upsampling
        float parityFrequencyBark = hzToBark(This->pixelBinParityFrequency);
        float upsampledPixelsFraction = (parityFrequencyBark - minBark) / (maxBark - minBark);
        This->upsampledPixels = 1 + round(1.0 * upsampledPixelsFraction * outputLength);
        if(This->upsampledPixels > outputLength) This->upsampledPixels = outputLength;
    }
    
    /*************************************
     * now that we have the fft bin indices we can interpolate and get the results
     *************************************/
    size_t numFFTBins = 1 + fftSize/2;
    assert(numFFTBins + This->fftBinInterpolationPadding - 2 >= floor(interpolatedIndices[outputLength-1])); // requirement of vDSP_vqint. can be ignored if there is extra space at the end of the fftBins array and zeros are written there.
    vDSP_vqint(fftBins, interpolatedIndices, 1, output, 1, outputLength, numFFTBins);
    // upsample the lower part of the image
//    vDSP_vqint(fftBins, interpolatedIndices, 1, output, 1, This->upsampledPixels, numFFTBins);
    // if we didn't get it all, downsample the upper half
    if (This->upsampledPixels < outputLength){
        // downsample from This->upsampledPixels to (outputLength - 1)
        for(size_t i=This->upsampledPixels; i<outputLength; i++){
            // get the max of all bins represented by this pixel
            float outval = 0.0f;
            for(size_t j=0; j < binsPerPixel[i]; j++){
                float binJ = fftBins[integerIndices[i] + j - binsPerPixel[i]/2];
                if(binJ > outval) outval = binJ;
            }
//                outval += fftBins[integerIndices[i] + j - binsPerPixel[i]/2];
            output[i] = outval;
        }
    }
}





void BMSpectrogram_toHSBColour(float* input, BMHSBPixel* output, size_t length){
    // toHSBColourFunction[x_] := {7/12, 1 - x, x}
    float h = 7.0/12.0;
    for(size_t i=0; i<length; i++)
        output[i] = simd_make_float3(h,1-input[i],input[i]);
}



// http://www.chilliant.com/rgb2hsv.html
/*
 float3 HUEtoRGB(in float H)
  {
    float R = abs(H * 6 - 3) - 1;
    float G = 2 - abs(H * 6 - 2);
    float B = 2 - abs(H * 6 - 4);
    return saturate(float3(R,G,B));
  }
 */
simd_float3 BMSpectrum_HUEtoRGB(float h) {
    simd_float3 a = {-1.0f, 2.0f, 2.0f};
    simd_float3 b = {-3.0f, -2.0f, -4.0f};
    simd_float3 c = {1.0f, -1.0f, -1.0f};
    simd_float3 rgb = simd_abs(6.0f * h + b) * c;
    return simd_clamp(rgb+a,0.0f, 1.0f);
}



// http://www.chilliant.com/rgb2hsv.html
simd_float3 BMSpectrum_HSLToRGB(simd_float3 hsl){
    // generate an rgb pixel with 100% saturation
    simd_float3 rgb = BMSpectrum_HUEtoRGB(hsl.x);
    
    // find out how much we need to scale down the saturated pixel to apply the
    // saturation and make headroom for the lightness
    float c = (1.0f - fabsf(2.0f * hsl.z - 1.0f)) * hsl.y;
    
    // scale the rgb pixel and mix with the lightness
    return (rgb - 0.5f) * c + hsl.z;
}



/*!
 *BMSpectrum_valueToRGBA
 *
 * let the value, v, be the lightness of a pixel in HSL space. We then create
 * a pixel in HSL and convert to RGBA.
 */
void BMSpectrum_valueToRGBA(float v, uint8_t *output){
    // generate an rgb pixel with 100% saturation
    simd_float3 rgb = {0.0f, 0.433f, 1.0f};
              //rgb(51, 139, 255)
    
    // find out how much we need to scale down the saturated pixel to apply the
    // saturation and make headroom for the lightness
    float s = 0.5f;
    float c = (1.0f - fabsf(2.0f * v - 1.0f)) * s;

    // scale the rgb pixel and mix with the lightness
    rgb = (rgb - 0.5f) * c + v;
    
    // convert to 8 bit RGBA and return
    rgb *= 255.0;
    output[0] = round(rgb.x);
    output[1] = round(rgb.y);
    output[2] = round(rgb.z);
    output[3] = 255;
}

//float h1 = 214.0/360.0;
//float s1 = 80.0/100.0;
//float b1 = 100.0/100.0;



void BMSpectrogram_toRGBAColour(float* input, uint8_t *output, size_t pixelWidth, size_t pixelHeight){
    size_t outputIncrement = pixelWidth * 4;
    int i = (int)pixelHeight - 1;
    for(; i>=0; i--){
        BMSpectrum_valueToRGBA(input[i], output);
        output += outputIncrement;
    }
}





void BMSpectrogram_toDbScaleAndClip(const float* input, float* output, size_t fftSize, size_t length){
    // compensate for the scaling of the vDSP FFT
    float scale = 32.0f / (float)fftSize;
    vDSP_vsmul(input, 1, &scale, output, 1, length);
    
    // eliminate zeros and negative values
    float threshold = BM_DB_TO_GAIN(-110);
    vDSP_vthr(output, 1, &threshold, output, 1, length);
    
    // convert to db
    float zeroDb = sqrt((float)fftSize)/2.0f;
    uint32_t use20not10 = 1;
    vDSP_vdbcon(output, 1, &zeroDb, output, 1, length, use20not10);
    
    // clip to [-100,0]
    float min = -100.0f;
    float max = 0.0f;
    vDSP_vclip(output, 1, &min, &max, output, 1, length);
    
    // shift and scale to [0,1]
    scale = 1.0f/100.0f;
    float shift = 1.0f;
    vDSP_vsmsa(output, 1, &scale, &shift, output, 1, length);
}




float BMSpectrogram_getPaddingLeft(size_t fftSize){
    if(4 > fftSize)
        printf("[BMSpectrogram] WARNING: fftSize must be >=4\n");
    if(!isPowerOfTwo((size_t)fftSize))
        printf("[BMSpectrogram] WARNING: fftSize must be a power of two\n");
    return (fftSize/2) - 1;
}


float BMSpectrogram_getPaddingRight(size_t fftSize){
    if(4 > fftSize)
        printf("[BMSpectrogram] WARNING: fftSize must be >=4\n");
    if(!isPowerOfTwo((size_t)fftSize))
        printf("[BMSpectrogram] WARNING: fftSize must be a power of two\n");
    return fftSize/2;
}




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
                           float maxFrequency){
    assert(4 <= fftSize && fftSize <= This->maxFFTSize);
    assert(isPowerOfTwo((size_t)fftSize));
    assert(2 <= pixelHeight && pixelHeight < This->maxImageHeight);
    
    // we will need this later
    SInt32 fftOutputSize = 1 + fftSize / 2;
    
    // check that the fft start and end indices are within the bounds of the input array
    // note that we define the fft centred at sample n to mean that the fft is
    // calculated on samples in the interval [n-(fftSize/2)+1, n+(fftSize/2)]
    SInt32 fftStartIndex = startSampleIndex - (fftSize/2) + 1;
    SInt32 fftEndIndex = endSampleIndex + (fftSize/2);
    assert(fftStartIndex >= 0 && fftEndIndex < inputLength);
    
    // calculate the FFT stride to get the pixelWidth. this is the number of
    // samples we shift the fft window each time we compute it. It may take a
    // non-integer value. If stride is a non-integer then the actual stride used
    // will still be an integer but it will vary between two consecutive values.
    float sampleWidth = endSampleIndex - startSampleIndex + 1;
    float fftStride = sampleWidth / (float)(pixelWidth-1);
    
    
    // generate the image one column at a time
    for(SInt32 i=0; i<pixelWidth; i++){
        // find the index where the current fft window starts
        SInt32 fftCurrentWindowStart = (SInt32)roundf((float)i*fftStride + FLT_EPSILON) + fftStartIndex;
        
        // set the last window to start in the correct place in case the multiplication above isn't accurate
        if(i==pixelWidth-1)
            fftCurrentWindowStart = fftEndIndex - fftSize + 1;
        
        // take abs(fft(windowFunctin*windowSamples))
        This->b1[fftOutputSize-1] = BMSpectrum_processDataBasic(&This->spectrum,
                                                          inputAudio + fftCurrentWindowStart,
                                                          This->b1,
                                                          true,
                                                          fftSize);
        
        // write some zeros after the end as padding for the interpolation function
        memset(This->b1 + fftOutputSize, 0, sizeof(float)*This->fftBinInterpolationPadding);
        
        // convert to dB, scale to [0,1] and clip values outside that range
        BMSpectrogram_toDbScaleAndClip(This->b1, This->b1, fftSize, fftOutputSize);
        
        // interpolate the output from linear scale frequency to Bark Scale
        BMSpectrogram_fftBinsToBarkScale(This,
                                         This->b1,
                                          This->b2,
                                         fftSize,
                                         pixelHeight,
                                         minFrequency,
                                         maxFrequency);
        
        // clamp the outputs to [0,1]
        float lowerLimit = 0;
        float upperLimit = 1;
        vDSP_vclip(This->b2, 1, &lowerLimit, &upperLimit, This->b2, 1, pixelHeight);
        
        // convert to RGBA colours and write to output
        BMSpectrogram_toRGBAColour(This->b2,&imageOutput[i*BMSG_BYTES_PER_PIXEL],pixelWidth, pixelHeight);
    }
}

size_t nearestPowerOfTwo(float x){
    float lower = pow(2.0f,floor(log2(x)));
    float upper = pow(2.0f,ceil(log2(x)));
    return x-lower<upper-x? lower:upper;
}

size_t BMSpectrogram_GetFFTSizeFor(BMSpectrogram *This, size_t pixelWidth, size_t sampleWidth){
    // set this to the fft size we want
    // when pixelWidth = sampleWidth
    float k = 128.0;

    // estimate the ideal fft size
    float fftSizeFloat = k * (float)sampleWidth / (float)pixelWidth;

    // round to the nearest size we can use (power of two)
    size_t fftSize = nearestPowerOfTwo(fftSizeFloat);

    // don't allow it to be longer than the max or shorter
    // than the min
    fftSize = SG_MIN(fftSize, This->maxFFTSize);
    fftSize = SG_MAX(fftSize, 128);
    return fftSize;
}




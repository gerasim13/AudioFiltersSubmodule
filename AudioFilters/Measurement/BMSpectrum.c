//
//  BMSpectrum.c
//  Saturator
//
//  Created by Nguyen Minh Tien on 1/31/18.
//  Copyright © 2018 TienNM. All rights reserved.
//

#include "BMSpectrum.h"
#include <assert.h>
#include "Constants.h"

#define BM_SPECTRUM_DEFAULT_BUFFER_LENGTH 1024

void BMSpectrum_setupFFT(BMSpectrum* this,size_t n);

bool IsPowerOfTwo(size_t x)
{
    return (x & (x - 1)) == 0;
}

size_t logToI(size_t index){
    int targetlevel = 0;
    while (index >>= 1) ++targetlevel;
    return targetlevel;
}

void BMSpectrum_init(BMSpectrum* this){
    this->fft_initialized = false;
    BMSpectrum_setupFFT(this,BM_SPECTRUM_DEFAULT_BUFFER_LENGTH);
}

void BMSpectrum_setupFFT(BMSpectrum* this,size_t n){
    if(this->fft_initialized){
        vDSP_destroy_fftsetup(this->setup);
        
        free(this->fft_input_buffer1);
        free(this->fft_input_buffer2);
        free(this->fft_output_buffer1);
        free(this->fft_output_buffer2);
    }
    //FFT
    assert(IsPowerOfTwo(n));
    
    const size_t log2n = logToI(n); // 2^10 = 1024
    printf("%zu %zu\n",log2n,n);
    
    //input
    this->fft_input_buffer1 = malloc((n/2)*sizeof(float)+15);
    this->fft_input.realp = (float*)(((uintptr_t)this->fft_input_buffer1+15) & ~ (uintptr_t)0x0F);
    this->fft_input_buffer2 = malloc((n/2)*sizeof(float)+15);
    this->fft_input.imagp = (float*)(((uintptr_t)this->fft_input_buffer2+15) & ~ (uintptr_t)0x0F);
    //output
    this->fft_output_buffer1 = malloc((n/2)*sizeof(float)+15);
    this->fft_output.realp = (float*)(((uintptr_t)this->fft_output_buffer1+15) & ~ (uintptr_t)0x0F);
    this->fft_output_buffer2 = malloc((n/2)*sizeof(float)+15);
    this->fft_output.imagp = (float*)(((uintptr_t)this->fft_output_buffer2+15) & ~ (uintptr_t)0x0F);
    //Buffer
    this->fft_buffer_buffer1 = malloc((n/2)*sizeof(float)+15);
    this->fft_buffer.realp = (float*)(((uintptr_t)this->fft_buffer_buffer1+15) & ~ (uintptr_t)0x0F);
    this->fft_buffer_buffer2 = malloc((n/2)*sizeof(float)+15);
    this->fft_buffer.imagp = (float*)(((uintptr_t)this->fft_buffer_buffer2+15) & ~ (uintptr_t)0x0F);
    
    // prepare the fft algo (you want to reuse the setup across fft calculations)
    this->setup = vDSP_create_fftsetup(log2n, kFFTRadix2);
    
    this->fft_initialized = true;
    
    this->windowData = malloc(sizeof(float)*n);
    vDSP_hamm_window(this->windowData, n, 0);
}

bool BMSpectrum_processData(BMSpectrum* this,float* inData,float* outData,int inSize,int* outSize,float* nq){
    if(inSize>0){
        //Output size = half input size
        *outSize = inSize/2.;
        
        const size_t log2n = logToI(inSize);
        
        //        assert(dataSize!=DesiredBufferLength);
        
        // apply a window function to fade the edges to zero
        vDSP_vmul(inData, 1, this->windowData, 1, inData, 1, inSize);
        
        // copy the input to the packed complex array that the fft algo uses
        vDSP_ctoz((DSPComplex *) inData, 2, &this->fft_input, 1, inSize/2.);
        
        // calculate the fft
        //        vDSP_fft_zrip(setup, &a, 1, log2n, FFT_FORWARD);
//        vDSP_fft_zropt(this->setup, &this->fft_input, 1, &this->fft_output, 1, &this->fft_buffer, log2n, FFT_FORWARD);
        vDSP_fft_zrop(this->setup, &this->fft_input, 1, &this->fft_output, 1, log2n, FFT_FORWARD);
        
        *nq = this->fft_output.imagp[0];
        this->fft_output.imagp[0] = 0;
        // give a nice name and reuse the buffer for output
        vDSP_zvabs(&this->fft_output, 1, outData, 1, inSize/2);
        
        //Limit the point
        float minValue = 0.0001;//-40db
        float maxValue = 1000;
        vDSP_vclip(outData, 1, &minValue, &maxValue, outData, 1, inSize/2);
        float b = 0.01;
        vDSP_vdbcon(outData, 1, &b, outData, 1, inSize/2, 1);
        
        *nq = 20* log10f(*nq/b);
        return true;
    }
    return false;
}


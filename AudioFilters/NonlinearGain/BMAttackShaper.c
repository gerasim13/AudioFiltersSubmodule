//
//  BMAttackShaper.c
//  AudioFiltersXcodeProject
//
//  Created by Hans on 18/2/20.
//  Copyright © 2020 BlueMangoo. All rights reserved.
//

#include <Accelerate/Accelerate.h>
#include "BMAttackShaper.h"
#include "Constants.h"


void BMAttackShaper_init(BMAttackShaper *This,
                         bool isStereo,
                         float sampleRate){
//    BMReleaseFilter rf1, rf2;
//    BMMultiLevelBiquad lpf;
//    BMDynamicSmoothingFilter dsf;
//    BMShortSimpleDelay dly;
    
    // set the delay to 12 samples at 48 KHz sampleRate or
    // stretch appropriately for other sample rate
    size_t numChannels = isStereo ? 2 : 1;
    size_t delaySamples = sampleRate * 12.0f / 48000.0f;
    BMShortSimpleDelay_init(&This->dly, numChannels, delaySamples);
}



void BMAttackShaper_processMono(BMAttackShaper *This,
                                const float* input,
                                float* output,
                                size_t numSamples){
    
    while(numSamples > 0){
        size_t samplesProcessing = BM_MIN(BM_BUFFER_CHUNK_SIZE,numSamples);

        /*******************
         * volume envelope *
         *******************/
        // absolute value
        vDSP_vabs(input, 1, This->b1, 1, samplesProcessing);
        //
        // release filter
        BMReleaseFilter_processBuffer(&This->rf1, This->b1, This->b1, samplesProcessing);
        //
        // lowpass filter
        BMMultiLevelBiquad_processBufferMono(&This->lpf, This->b1, This->b1, samplesProcessing);
        
        
        /*************************************************************
         * control signal to force the input down below the envelope *
         *************************************************************/
        // b1 / (abs(input) + 0.0001)
        float smallNumber = 0.0001;
        vDSP_vabs(input,1,This->b2,1,samplesProcessing);
        vDSP_vsadd(This->b2, 1, &smallNumber, This->b2, 1, samplesProcessing);
        vDSP_vdiv(This->b2, 1, This->b1, 1, This->b1, 1, samplesProcessing);
        //
        // limit the control signal value below 1 so that it can reduce but not increase volume
        vDSP_vneg(This->b1, 1, This->b1, 1, samplesProcessing);
        float negOne = -1.0f;
        vDSP_vthr(This->b1, 1, &negOne, This->b1, 1, samplesProcessing);
        vDSP_vneg(This->b1, 1, This->b1, 1, samplesProcessing);
        
        
        /************************************************
         * filter the control signal to reduce aliasing *
         ************************************************/
        // release filter
        BMReleaseFilter_processBuffer(&This->rf2, This->b1, This->b1, samplesProcessing);
        //
        // dynamic smoothing
        BMDynamicSmoothingFilter_processBuffer(&This->dsf, This->b1, This->b1, samplesProcessing);
        
        
        /************************************************************************
         * delay the input signal to enable faster response time without clicks *
         ************************************************************************/
        const float **inputs = &input;
        float **outputs = &This->b2;
        BMShortSimpleDelay_process(&This->dly, inputs, outputs, 1, samplesProcessing);
        
        
        /********************************************************
         * apply the volume control signal to the delayed input *
         ********************************************************/
        vDSP_vmul(This->b2, 1, This->b1, 1, output, 1, samplesProcessing);
        
        
        /********************
         * advance pointers *
         ********************/
        input += samplesProcessing;
        output += samplesProcessing;
        numSamples -= samplesProcessing;
    }
}

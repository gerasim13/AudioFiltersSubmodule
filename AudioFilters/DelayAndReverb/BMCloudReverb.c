//
//  BMCloudReverb.c
//  AUCloudReverb
//
//  Created by Nguyen Minh Tien on 2/27/20.
//  Copyright © 2020 BlueMangoo. All rights reserved.
//

#include "BMCloudReverb.h"
#include "BMReverb.h"
#include "BMFastHadamard.h"


#define Filter_Level_Lowshelf 0
#define Filter_Level_Tone 1
#define Filter_Level_Lowpass10k 2
#define Filter_Level_TotalLP 3

#define Filter_TotalLevel Filter_Level_Lowpass10k + Filter_Level_TotalLP

#define Filter_LS_FC 400
#define PitchShift_BaseNote 0.14f
#define PitchShift_BaseDuration 10.0f
#define ReadyNo 98573
#define FDN_BaseMaxDelaySecond 0.800f

void BMCloudReverb_updateDiffusion(BMCloudReverb* This);
void BMCloudReverb_prepareLoopDelay(BMCloudReverb* This);
void BMCloudReverb_updateLoopGain(BMCloudReverb* This,size_t* delayTimeL,size_t* delayTimeR,float* gainL,float* gainR);
float calculateScaleVol(BMCloudReverb* This);
void BMCloudReverb_updateVND(BMCloudReverb* This);

float getVNDLength(float numTaps,float length){
    float vndLength = ((numTaps*numTaps)*length)/(1 + numTaps + numTaps*numTaps);
    return vndLength;
}

float randomAround1(int percent){
    int rdPercent = percent - 1 - (arc4random()%(percent*2 - 1)) + 100;
    float rand = rdPercent/100.0f;
    return rand;
}

void BMCloudReverb_init(BMCloudReverb* This,float sr){
    This->sampleRate = sr;
    //BIQUAD FILTER
    BMMultiLevelBiquad_init(&This->biquadFilter, Filter_TotalLevel, sr, true, false, true);
    //Tone control - use 6db
	BMCloudReverb_setHighCutFreq(This, 1200.0f);
    //lowpass 36db
	BMMultiLevelBiquad_setHighOrderBWLP(&This->biquadFilter, 9300, Filter_Level_Lowpass10k, Filter_Level_TotalLP);
    //Low shelf
    This->lsGain = 0;
	BMCloudReverb_setLSGain(This, This->lsGain);
    
    //VND
    This->updateVND = false;
    This->maxTapsEachVND = 16;
    This->diffusion = 1.0f;
    This->vndLength = 0.2f;
    This->fadeInS = 0;
    This->vndDryTap = false;
    
    This->numInput = 8;
    This->numVND = This->numInput;
    This->vndArray = malloc(sizeof(BMVelvetNoiseDecorrelator)*This->numVND);
    This->vnd1BufferL = malloc(sizeof(float*)*This->numInput);
    This->vnd1BufferR = malloc(sizeof(float*)*This->numInput);
    This->vnd2BufferL = malloc(sizeof(float*)*This->numInput);
    This->vnd2BufferR = malloc(sizeof(float*)*This->numInput);
    
    //Using vnd
    for(int i=0;i<This->numVND;i++){
        //First layer
        BMVelvetNoiseDecorrelator_initWithEvenTapDensity(&This->vndArray[i], This->vndLength, This->maxTapsEachVND, 100, This->vndDryTap, sr);
        BMVelvetNoiseDecorrelator_setFadeIn(&This->vndArray[i], 0.0f);
        if(This->vndDryTap)
            BMVelvetNoiseDecorrelator_setWetMix(&This->vndArray[i], 1.0f);
        
        This->vnd1BufferL[i] = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
        This->vnd1BufferR[i] = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
        This->vnd2BufferL[i] = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
        This->vnd2BufferR[i] = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    }
    
    //Pitch shifting
    size_t delayRange = (1000*sr)/48000.0f;
    size_t maxDelayRange = (20000*sr)/48000.0f;
    float duration = 3.5f;
    BMPitchShiftDelay_init(&This->pitchShiftDelay, duration,delayRange ,maxDelayRange , sr, false, true);
    
    BMPitchShiftDelay_setWetGain(&This->pitchShiftDelay, 1.0f);
    BMPitchShiftDelay_setDelayDuration(&This->pitchShiftDelay,duration);
    BMPitchShiftDelay_setDelayRange(&This->pitchShiftDelay, delayRange);
    BMPitchShiftDelay_setBandLowpass(&This->pitchShiftDelay, 4000);
    BMPitchShiftDelay_setBandHighpass(&This->pitchShiftDelay,120);
    BMPitchShiftDelay_setMixOtherChannel(&This->pitchShiftDelay, 1.0f);

    
    
    This->buffer.bufferL = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->buffer.bufferR = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->LFOBuffer.bufferL = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->LFOBuffer.bufferR = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->loopInput.bufferL = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->loopInput.bufferR = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->lastLoopBuffer.bufferL = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->lastLoopBuffer.bufferR = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    memset(This->lastLoopBuffer.bufferL, 0, sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    memset(This->lastLoopBuffer.bufferR, 0, sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    
    This->wetBuffer.bufferL = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->wetBuffer.bufferR = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    
    //Loop delay
    BMCloudReverb_prepareLoopDelay(This);
    
    BMCloudReverb_setLoopDecayTime(This, 10);
    BMCloudReverb_setDelayPitchMixer(This, 0.5f);
    BMWetDryMixer_init(&This->reverbMixer, sr);
    BMCloudReverb_setOutputMixer(This, 0.5f);
    
    //Pan
    BMPanLFO_init(&This->inputPan, 0.412f, 0.6f, sr,true);
    BMPanLFO_init(&This->outputPan, 0.5456f, 0.6f, sr,true);
    
    This->initNo = ReadyNo;
}

void BMCloudReverb_prepareLoopDelay(BMCloudReverb* This){
    size_t numDelays = 16;
    float maxDT = FDN_BaseMaxDelaySecond;
    float minDT = 0.020f;
	bool zeroTaps = true;
    BMLongLoopFDN_init(&This->loopFDN, numDelays, minDT, maxDT, zeroTaps, 8, 1,100, This->sampleRate);
}

void BMCloudReverb_destroy(BMCloudReverb* This){
    BMMultiLevelBiquad_free(&This->biquadFilter);
    
    for(int i=0;i<This->numVND;i++){
        BMVelvetNoiseDecorrelator_free(&This->vndArray[i]);
    }
    for(int i=0;i<This->numInput;i++){
        free(This->vnd1BufferL[i]);
        This->vnd1BufferL[i] = nil;
        free(This->vnd1BufferR[i]);
        This->vnd1BufferR[i] = nil;
        free(This->vnd2BufferL[i]);
        This->vnd2BufferL[i] = nil;
        free(This->vnd2BufferR[i]);
        This->vnd2BufferR[i] = nil;
    }
    
    free(This->vnd1BufferL);
    free(This->vnd1BufferR);
    free(This->vnd2BufferL);
    free(This->vnd2BufferR);
    
    BMPitchShiftDelay_destroy(&This->pitchShiftDelay);
    
    BMLongLoopFDN_free(&This->loopFDN);
    
    free(This->buffer.bufferL);
    This->buffer.bufferL = nil;
    free(This->buffer.bufferR);
    This->buffer.bufferR = nil;
    free(This->LFOBuffer.bufferL);
    This->LFOBuffer.bufferL = nil;
    free(This->LFOBuffer.bufferR);
    This->LFOBuffer.bufferR = nil;
    free(This->loopInput.bufferL);
    This->loopInput.bufferL = nil;
    free(This->loopInput.bufferR);
    This->loopInput.bufferR = nil;
    free(This->lastLoopBuffer.bufferL);
    This->lastLoopBuffer.bufferL = nil;
    free(This->lastLoopBuffer.bufferR);
    This->lastLoopBuffer.bufferR = nil;
    free(This->wetBuffer.bufferL);
    This->wetBuffer.bufferL = nil;
    free(This->wetBuffer.bufferR);
    This->wetBuffer.bufferR = nil;
}

void BMCloudReverb_processStereo(BMCloudReverb* This,float* inputL,float* inputR,float* outputL,float* outputR,size_t numSamples,bool offlineRendering){
    if(This->initNo==ReadyNo){
        assert(numSamples<=BM_BUFFER_CHUNK_SIZE);
        
        BMCloudReverb_updateVND(This);
        BMCloudReverb_updateDiffusion(This);
        
        //Filters
        BMMultiLevelBiquad_processBufferStereo(&This->biquadFilter, inputL, inputR, This->buffer.bufferL, This->buffer.bufferR, numSamples);
        
//        memcpy(This->buffer.bufferL, inputL, sizeof(float)*numSamples);
//        memcpy(This->buffer.bufferR, inputR, sizeof(float)*numSamples);
        
        //1st layer VND
        for(int i=0;i<This->numInput;i++){
            //PitchShifting delay into wetbuffer
            BMVelvetNoiseDecorrelator_processBufferStereo(&This->vndArray[i], This->buffer.bufferL, This->buffer.bufferR, This->vnd1BufferL[i], This->vnd1BufferR[i], numSamples);
        }
        
        if(This->numInput>4){
            BMFastHadamardTransformBuffer(This->vnd1BufferL, This->vnd2BufferL, This->numInput, numSamples);
            BMFastHadamardTransformBuffer(This->vnd1BufferR, This->vnd2BufferR, This->numInput, numSamples);
        }else{
            for(int j=0;j<This->numInput;j++){
                memcpy(This->vnd2BufferL[j], This->vnd1BufferL[j], sizeof(float)*numSamples);
                memcpy(This->vnd2BufferR[j], This->vnd1BufferR[j], sizeof(float)*numSamples);
            }
        }

        
        //Long FDN
        BMLongLoopFDN_processMultiChannelInput(&This->loopFDN, This->vnd2BufferL, This->vnd2BufferR, This->numInput, This->wetBuffer.bufferL, This->wetBuffer.bufferR, numSamples);
		
        BMPitchShiftDelay_processStereoBuffer(&This->pitchShiftDelay, This->wetBuffer.bufferL, This->wetBuffer.bufferR, This->wetBuffer.bufferL, This->wetBuffer.bufferR, numSamples);
        
//        memcpy(outputL,This->wetBuffer.bufferL, sizeof(float)*numSamples);
//        memcpy(outputR,This->wetBuffer.bufferR , sizeof(float)*numSamples);
//        return;
        
        vDSP_vsmul(This->wetBuffer.bufferL, 1, &This->normallizeVol, This->wetBuffer.bufferL, 1, numSamples);
        vDSP_vsmul(This->wetBuffer.bufferR, 1, &This->normallizeVol, This->wetBuffer.bufferR, 1, numSamples);
        
        if(offlineRendering){
            float dryMix = 1 - This->reverbMixer.mixTarget;
            vDSP_vsmsma(This->wetBuffer.bufferL, 1, &This->reverbMixer.mixTarget, inputL, 1, &dryMix, outputL, 1, numSamples);
            vDSP_vsmsma(This->wetBuffer.bufferR, 1, &This->reverbMixer.mixTarget, inputR, 1, &dryMix, outputR, 1, numSamples);
        }else{
            //Process reverb dry/wet mixer
            BMWetDryMixer_processBufferInPhase(&This->reverbMixer, This->wetBuffer.bufferL, This->wetBuffer.bufferR, inputL, inputR, outputL, outputR, numSamples);
        }

        
        //LFO pan
		// Input pan LFO
        BMPanLFO_process(&This->inputPan, This->LFOBuffer.bufferL, This->LFOBuffer.bufferR, numSamples);
        vDSP_vmul(outputL, 1, This->LFOBuffer.bufferL, 1, outputL, 1, numSamples);
        vDSP_vmul(outputR, 1, This->LFOBuffer.bufferR, 1, outputR, 1, numSamples);
        BMPanLFO_process(&This->outputPan, This->LFOBuffer.bufferL, This->LFOBuffer.bufferR, numSamples);
        vDSP_vmul(outputL, 1, This->LFOBuffer.bufferL, 1, outputL, 1, numSamples);
        vDSP_vmul(outputR, 1, This->LFOBuffer.bufferR, 1, outputR, 1, numSamples);
    }
}

float calculateScaleVol(BMCloudReverb* This){
    float factor = log2f(This->decayTime);
    float scaleDB = (-3. * (factor)) + (1.2f-This->diffusion)*8.0f;
//    printf("%f\n",scaleDB);
    return scaleDB;
}

#pragma mark - Set
void BMCloudReverb_setLoopDecayTime(BMCloudReverb* This,float decayTime){
    This->decayTime = decayTime;
    BMLongLoopFDN_setRT60Decay(&This->loopFDN, decayTime);
    This->normallizeVol = BM_DB_TO_GAIN(calculateScaleVol(This));
}

#define DepthMax 0.25f
float BMCloudReverb_getMixBaseOnMode(BMCloudReverb* This,float mode){
    float mix = 0;
    if(mode==0){
        mix = 0.1f;
    }else if(mode==1){
        mix = 0.2f;
    }else if(mode==2){
        mix = 0.35f;
    }else if(mode==3){
        mix = 0.4f;
    }else if(mode==4){
        mix = 0.42f;
    }else if(mode==5){
        mix = 0.5f;
    }
    return mix;
}

float BMCloudReverb_getDelayRangeBaseOnMode(BMCloudReverb* This,float mode){
    float range = 0;
    if(mode==5){
        //Mode 6
        range = 1600;
    }else if(mode==4){
        range = 1250;
    }else{
        range = 1000;
    }
    return (range/48000.0f)*This->sampleRate;
}

#define S_ChorusMode_Min 0
#define S_ChorusMode_Max 5
#define DC_Duration_Max 10.5f
#define DC_Duration_Min 3.5f

float BMCloudReverb_getDurationBaseOnMode(BMCloudReverb* This,float mode){
    float duration = (DC_Duration_Max-DC_Duration_Min)*(1 - mode/(S_ChorusMode_Max-S_ChorusMode_Min)) + DC_Duration_Min;
    if(mode==5){
        duration = duration * 1.6f;
    }else if(mode==4){
        duration = duration * 1.25f;
    }
    return duration;
}

void BMCloudReverb_setDelayPitchMixer(BMCloudReverb* This,float wetMix){
    //Set delayrange of pitch shift to control speed of pitch shift
    float mode = roundf(wetMix*5.0f);
    
    //mix from 0.1 to 0.5
    float mix = BMCloudReverb_getMixBaseOnMode(This,mode);
    //Speed from 1/30 -> 1/10
    float duration = BMCloudReverb_getDurationBaseOnMode(This,mode);
    
    float delayRange = BMCloudReverb_getDelayRangeBaseOnMode(This,mode);
    printf("pitch %f %f %f\n",mix,duration,delayRange);
    
    BMPitchShiftDelay_setWetGain(&This->pitchShiftDelay, mix);
    BMPitchShiftDelay_setDelayDuration(&This->pitchShiftDelay,duration);
    BMPitchShiftDelay_setDelayRange(&This->pitchShiftDelay, delayRange);
    
    //Control pan
    BMPanLFO_setDepth(&This->inputPan, wetMix*DepthMax);
    BMPanLFO_setDepth(&This->outputPan, wetMix*DepthMax);
}



void BMCloudReverb_setOutputMixer(BMCloudReverb* This,float wetMix){
    BMWetDryMixer_setMix(&This->reverbMixer, wetMix*wetMix);
}

void BMCloudReverb_setDiffusion(BMCloudReverb* This,float diffusion){
    if(This->diffusion!=diffusion){
        This->diffusion = diffusion;
        This->updateDiffusion = true;
        This->numInput = BM_MAX((roundf(8 * diffusion)/2.0f)*2,2);
        
        //Diff go from 0.1 to 1
        float maxDelayS = FDN_BaseMaxDelaySecond*(1.1f-diffusion);
        BMLongLoopFDN_setMaxDelay(&This->loopFDN, maxDelayS);
        //update wet vol
        This->normallizeVol = BM_DB_TO_GAIN(calculateScaleVol(This));
    }
}

void BMCloudReverb_updateDiffusion(BMCloudReverb* This){
    if(This->updateDiffusion){
        This->updateDiffusion = false;
        float numTaps = floorf(This->maxTapsEachVND * This->diffusion);
        for(int i=0;i<This->numVND;i++){
            BMVelvetNoiseDecorrelator_setNumTaps(&This->vndArray[i], numTaps);
        }
    }
}

void BMCloudReverb_setLSGain(BMCloudReverb* This,float gainDb){
    BMMultiLevelBiquad_setLowShelfFirstOrder(&This->biquadFilter, Filter_LS_FC, gainDb, Filter_Level_Lowshelf);
}

void BMCloudReverb_setHighCutFreq(BMCloudReverb* This,float freq){
    BMMultiLevelBiquad_setLowPass6db(&This->biquadFilter, freq, Filter_Level_Tone);
}

#pragma mark - VND
void BMCloudReverb_setFadeInVND(BMCloudReverb* This,float timeInS){
    This->fadeInS = timeInS;
    for(int i=0;i<This->numVND;i++){
        BMVelvetNoiseDecorrelator_setFadeIn(&This->vndArray[i], timeInS);
    }
}

void BMCloudReverb_setVNDLength(BMCloudReverb* This,float timeInS){
    This->vndLength = timeInS;
    This->updateVND = true;
}

void BMCloudReverb_updateVND(BMCloudReverb* This){
    if(This->updateVND){
        This->updateVND = false;
        //Free & reinit
        for(int i=0;i<This->numVND;i++){
            //1st layer
            BMVelvetNoiseDecorrelator_free(&This->vndArray[i]);
            BMVelvetNoiseDecorrelator_initWithEvenTapDensity(&This->vndArray[i], This->vndLength, This->maxTapsEachVND, 100, This->vndDryTap, This->sampleRate);
            if(This->vndDryTap)
                BMVelvetNoiseDecorrelator_setWetMix(&This->vndArray[i], 1.0f);
        }

        BMCloudReverb_setFadeInVND(This, This->fadeInS);
        BMCloudReverb_setDiffusion(This, This->diffusion);
    }
}

#pragma mark - Test
void BMCloudReverb_impulseResponse(BMCloudReverb* This,float* inputL,float* inputR,float* outputL,float* outputR,size_t length){
    size_t sampleProcessed = 0;
    size_t sampleProcessing = 0;
    This->biquadFilter.useSmoothUpdate = false;
    while(sampleProcessed<length){
        sampleProcessing = BM_MIN(BM_BUFFER_CHUNK_SIZE, length - sampleProcessed);
        
        BMCloudReverb_processStereo(This, inputL+sampleProcessed, inputR+sampleProcessed, outputL+sampleProcessed, outputR+sampleProcessed, sampleProcessing,true);
        if(outputL[sampleProcessed]>1.0f)
            printf("error\n");
        sampleProcessed += sampleProcessing;
    }
    This->biquadFilter.useSmoothUpdate = true;
}

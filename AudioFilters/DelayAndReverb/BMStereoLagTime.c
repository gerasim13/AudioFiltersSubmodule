//
//  BMStereoLagTime.c
//  BMAUStereoLagTime
//
//  Created by Nguyen Minh Tien on 3/5/19.
//  Copyright © 2019 Nguyen Minh Tien. All rights reserved.
//

#include "BMStereoLagTime.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <Accelerate/Accelerate.h>
#include "Constants.h"

static inline void BMStereoLagTime_vProcessOneChannel(BMStereoLagTime *This,TPCircularBuffer* circularBuffer,float* delaySamples, size_t desiredDS, float* tempBuffer,float* strideIdx,float* strideBuffer,float speed,size_t* samplesToReachTarget,float* inBuffer, float* outBuffer,size_t numSamples);
void BMStereoLagTime_prepareLGIBuffer(BMStereoLagTime *This,size_t bufferSize);

void BMStereoLagTime_init(BMStereoLagTime *This,size_t maxDelayTimeInMilSecond,size_t sampleRate){
    //init buffer
    This->sampleRate = sampleRate;
    This->maxDelaySamples = (uint32_t)ceilf((maxDelayTimeInMilSecond/1000. * sampleRate)/BM_BUFFER_CHUNK_SIZE) * BM_BUFFER_CHUNK_SIZE;
    TPCircularBufferInit(&This->bufferL, (This->maxDelaySamples + BM_BUFFER_CHUNK_SIZE)*sizeof(float));
    TPCircularBufferClear(&This->bufferL);
//    TPCircularBufferProduce(&This->bufferL, sizeof(float)*5);
    
    TPCircularBufferInit(&This->bufferR, (This->maxDelaySamples + BM_BUFFER_CHUNK_SIZE)*sizeof(float));
    TPCircularBufferClear(&This->bufferR);
//    TPCircularBufferProduce(&This->bufferR, sizeof(float)*5);
    
    This->delaySamplesL = 0;
    This->delaySamplesR = 0;
    This->desiredDSL = 0;
    This->desiredDSR = 0;
    This->targetDSL = 0;
    This->targetDSR = 0;
    This->shouldUpdateDS = false;
    This->strideIdxL = 0;
    This->strideIdxR = 0;
    This->speedL = 1;
    This->speedR = 1;
    This->strideBufferL = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    This->strideBufferR = malloc(sizeof(float)*BM_BUFFER_CHUNK_SIZE);
    
    BMStereoLagTime_prepareLGIBuffer(This, AudioBufferLength);
}

void BMStereoLagTime_prepareLGIBuffer(BMStereoLagTime *This,size_t bufferSize){
    BMLagrangeInterpolation_init(&This->lgInterpolation, LGI_Order);
    //Stride
    size_t strideSize = bufferSize * LGI_Order;
    This->lgiStrideIdx = malloc(sizeof(float) * strideSize);
    float steps = 1./LGI_Order;
    float baseIdx = LGI_Order* 0.5 - 1;
    for(int i=0;i<strideSize;i++){
        This->lgiStrideIdx[i] = i* steps + baseIdx;
    }
    
    //Temp buffer
    This->lgiBufferL = malloc(sizeof(float) * (bufferSize + LGI_Order));
    This->lgiBufferR = malloc(sizeof(float) * (bufferSize + LGI_Order));
    //memset(&lgiBuffer, 0, sizeof(lgiBuffer));
    
    This->lgiUpBuffer = malloc(sizeof(float) * bufferSize * LGI_Order);
}

void BMStereoLagTime_destroy(BMStereoLagTime *This){
    TPCircularBufferCleanup(&This->bufferL);
    TPCircularBufferCleanup(&This->bufferR);
    
    BMLagrangeInterpolation_destroy(&This->lgInterpolation);
}

void BMStereoLagTime_setDelayLeft(BMStereoLagTime *This,size_t delayInMilSecond){
    int32_t delaySample = delayInMilSecond/ 1000.  *This->sampleRate;
    if(delaySample!=This->targetDSL){
        This->targetDSL = delaySample;
        This->targetDSR = 0;
        This->shouldUpdateDS = true;
    }
}

void BMStereoLagTime_setDelayRight(BMStereoLagTime *This,size_t delayInMilSecond){
    int32_t delaySample = delayInMilSecond/ 1000.  *This->sampleRate;
    if(delaySample!=This->targetDSR){
        This->targetDSR = delaySample;
        This->targetDSL = 0;
        This->shouldUpdateDS = true;
    }
}

void BMStereoLagTime_updateTargetDelaySamples(BMStereoLagTime *This){
    if(This->shouldUpdateDS){
        This->shouldUpdateDS = false;
        This->desiredDSL = This->targetDSL;
        This->desiredDSR = This->targetDSR;
        //calculate speed
        This->sampleToReachTargetL = 0.5  *This->sampleRate;
        float dsIdxStep = (This->desiredDSL - This->delaySamplesL)/This->sampleToReachTargetL;
        This->speedL = 1 - dsIdxStep;
        
        This->sampleToReachTargetR = 0.5  *This->sampleRate;
        dsIdxStep = (This->desiredDSR - This->delaySamplesR)/This->sampleToReachTargetR;
        This->speedR = 1 - dsIdxStep;
        
//        if(This->speedL!=1&&This->speedR!=1)
//            printf("update %f %f\n",This->speedL,This->speedR);
        
//        printf("%d %d %d %d\n",This->delaySamplesL,This->desiredDSL,This->delaySamplesR,This->desiredDSR);
        if(This->targetDSL==0){
            This->isDelayLeftChannel = false;
        }else{
            This->isDelayLeftChannel = true;
        }
    }
}

void BMStereoLagTime_process(BMStereoLagTime *This,float* inL, float* inR, float* outL, float* outR,size_t numSamples){
    //Update delay sample target
    BMStereoLagTime_updateTargetDelaySamples(This);
    //Process
    float delaySamples = 0;
    if(This->isDelayLeftChannel){
        //Delay left channel -> check right channel still have delay samples first
        if(This->delaySamplesR>0){
            //Cpy left channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferL, &delaySamples, delaySamples, This->lgiBufferL,&This->strideIdxL,This->strideBufferL,This->speedL,&This->sampleToReachTargetL, inL, outL, numSamples);
            //Process right channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferR, &This->delaySamplesR, This->desiredDSR,This->lgiBufferR,&This->strideIdxR,This->strideBufferR,This->speedR,&This->sampleToReachTargetR, inR, outR, numSamples);
        }else{
            //Cpy right channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferR, &delaySamples, delaySamples,This->lgiBufferR,&This->strideIdxR,This->strideBufferR,This->speedR,&This->sampleToReachTargetR, inR, outR, numSamples);
            //Process left channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferL, &This->delaySamplesL, This->desiredDSL,This->lgiBufferL,&This->strideIdxL,This->strideBufferL,This->speedL,&This->sampleToReachTargetL, inL, outL, numSamples);
        }
    }else{
        //Delay right channel -> check left channel still have delay samples first
        if(This->delaySamplesL>0){
            //Cpy right channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferR, &delaySamples, delaySamples,This->lgiBufferR,&This->strideIdxR,This->strideBufferR,This->speedR,&This->sampleToReachTargetR, inR, outR, numSamples);
            //Process left channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferL, &This->delaySamplesL, This->desiredDSL,This->lgiBufferL,&This->strideIdxL,This->strideBufferL,This->speedL,&This->sampleToReachTargetL, inL, outL, numSamples);
        }else{
            //Cpy left channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferL, &delaySamples, delaySamples,This->lgiBufferL,&This->strideIdxL,This->strideBufferL,This->speedL,&This->sampleToReachTargetL, inL, outL, numSamples);
            
            //Process right channel
            BMStereoLagTime_vProcessOneChannel(This, &This->bufferR, &This->delaySamplesR, This->desiredDSR,This->lgiBufferR,&This->strideIdxR,This->strideBufferR,This->speedR,&This->sampleToReachTargetR, inR, outR, numSamples);
        }
        
    }
}

static inline void BMStereoLagTime_vProcessOneChannel(BMStereoLagTime *This,TPCircularBuffer* circularBuffer,float* delaySamples, size_t desiredDS, float* tempBuffer,float* strideIdx,float* strideBuffer,float speed,size_t* samplesToReachTarget,float* inBuffer, float* outBuffer,size_t numSamples){
    size_t samplesProcessing;
    size_t samplesProcessed = 0;
    while (samplesProcessed<numSamples) {
        samplesProcessing = BM_MIN(numSamples- samplesProcessed,BM_BUFFER_CHUNK_SIZE);
        
        //Process here
        uint32_t bytesAvailableForWrite;
        void* head = TPCircularBufferHead(circularBuffer, &bytesAvailableForWrite);
        
        // copy from input to the buffer head
        uint32_t byteProcessing = (uint32_t)(samplesProcessing * sizeof(float));
        memcpy(head, inBuffer + samplesProcessed, byteProcessing);
        
        // mark the written region of the buffer as written
        TPCircularBufferProduce(circularBuffer, byteProcessing);
        
        //Tail
        uint32_t bytesAvailableForRead;
        float* tail = TPCircularBufferTail(circularBuffer, &bytesAvailableForRead);
        
        float sampleToConsume = 0;
        
        if(*samplesToReachTarget>0&&*delaySamples!=desiredDS){//
            //Copy input to lgi buffer - skip 10 first order because we will reuse the last buffer data
            size_t inputLength = BM_MIN(bytesAvailableForRead/sizeof(float), ceilf(samplesProcessing * speed + *strideIdx + samplesProcessing));
            memcpy(tempBuffer + LGI_Order, tail, sizeof(float)*inputLength);
            
            float retainInputIdx = 0.0f;
            float baseIdx = LGI_Order * 0.5f;
            
            *strideIdx += baseIdx + speed - 1.0f;
            size_t sampleToRamp = BM_MIN(*samplesToReachTarget, samplesProcessing);
            vDSP_vramp(strideIdx, &speed, strideBuffer, 1, sampleToRamp);
            
            if(sampleToRamp<samplesProcessing){
                //Reach target
                retainInputIdx = roundf(*strideIdx + speed * (*samplesToReachTarget)) + 1;
                size_t retainSamples = samplesProcessing - (*samplesToReachTarget);
                sampleToConsume = retainInputIdx - baseIdx + retainSamples;
                *delaySamples += *samplesToReachTarget - sampleToConsume + 1;
                float step = 1;
                vDSP_vramp(&retainInputIdx, &step, strideBuffer+(*samplesToReachTarget), 1, retainSamples);
                *strideIdx = 0;
                *samplesToReachTarget = 0;
            }else{
                //not reach target yet
                *strideIdx += speed * (samplesProcessing - 1);
//                *strideIdx += speed * (samplesProcessing);
                sampleToConsume = floorf(*strideIdx) + 1 - baseIdx;
                *strideIdx -= floorf(*strideIdx);
                *delaySamples += samplesProcessing - sampleToConsume;
                *samplesToReachTarget -= samplesProcessing;
            }
            BMLagrangeInterpolation_processUpSample(&This->lgInterpolation, tempBuffer, strideBuffer, outBuffer + samplesProcessed, 1, inputLength + LGI_Order, samplesProcessing);
//            vDSP_vqint(tempBuffer, strideBuffer, 1, outBuffer + samplesProcessed, 1, samplesProcessing, inputLength + LGI_Order);
//            printf("%f %f\n",outBuffer[samplesProcessed],outBuffer[samplesProcessing-1 + samplesProcessed]);
        }else{
            // copy from buffer tail to output
            //copy 5 samples from buffer
            size_t samplesFromLastFrame = 5;
            memcpy(outBuffer+samplesProcessed, tempBuffer+samplesFromLastFrame, sizeof(float)*samplesFromLastFrame);
            //Copy the rest
            memcpy(outBuffer+samplesProcessed + samplesFromLastFrame, tail, byteProcessing-sizeof(float)*samplesFromLastFrame);
            
            sampleToConsume = samplesProcessing;
            
//            printf("%f %f\n",outBuffer[samplesProcessed],outBuffer[samplesProcessing-1 + samplesProcessed]);
        }
        
        //        printf("%f\n",sampleToConsume);
        //Save 10 last samples into lgiUpBuffer 10 first samples
        int samplesToStoreFromTail = sampleToConsume - LGI_Order;
        memcpy(tempBuffer, tail  + samplesToStoreFromTail, sizeof(float)*LGI_Order);
        
        // mark the read bytes as used
        TPCircularBufferConsume(circularBuffer, BM_MIN(sampleToConsume*sizeof(float),bytesAvailableForRead));
        
        //        float* nexttail = TPCircularBufferTail(circularBuffer, &bytesAvailableForRead);
        //        printf("next %f %lu\n",*delaySamples,bytesAvailableForRead/sizeof(float));
        
        samplesProcessed += samplesProcessing;
    }
}

//static inline void BMStereoLagTime_vProcessOneChannel(BMStereoLagTime *This,TPCircularBuffer* circularBuffer,float* delaySamples, size_t desiredDS, float* tempBuffer,float* strideIdx,float* strideBuffer,float speed,size_t* samplesToReachTarget,float* inBuffer, float* outBuffer,size_t numSamples)
//{
//    size_t samplesProcessing;
//    size_t samplesProcessed = 0;
//    while (samplesProcessed<numSamples) {
//        samplesProcessing = BM_MIN(numSamples- samplesProcessed,BM_BUFFER_CHUNK_SIZE);
//
//        //Process here
//        uint32_t bytesAvailableForWrite;
//        void* head = TPCircularBufferHead(circularBuffer, &bytesAvailableForWrite);
//
//        // copy from input to the buffer head
//        uint32_t byteProcessing = (uint32_t)(samplesProcessing * sizeof(float));
//        memcpy(head, inBuffer + samplesProcessed, byteProcessing);
//
//        // mark the written region of the buffer as written
//        TPCircularBufferProduce(circularBuffer, byteProcessing);
//
//        //Tail
//        uint32_t bytesAvailableForRead;
//        float* tail = TPCircularBufferTail(circularBuffer, &bytesAvailableForRead);
//
//        float sampleToConsume = 0;
//
//        if(samplesToReachTarget>0&&*delaySamples!=desiredDS){//
//            //Copy input to lgi buffer - skip 10 first order because we will reuse the last buffer data
//            size_t inputLength = BM_MIN(bytesAvailableForRead/sizeof(float), ceilf(samplesProcessing * speed + *strideIdx + samplesProcessing));
//            if(inputLength==bytesAvailableForRead/sizeof(float)){
//                int j = 0;
//                j++;
//            }
//            memcpy(tempBuffer + LGI_Order, tail, sizeof(float)*inputLength);
//
//            int retainInputIdx = 0;
//            float baseIdx = LGI_Order * 0.5;
//            bool reachTarget = false;
//            *strideIdx += baseIdx;
//            for(int i=0;i<samplesProcessing;i++){
//                if(i<=*samplesToReachTarget){
//                    //Speed mode -> go faster or slower to reach Desired Delay Samples
//                    outBuffer[i+samplesProcessed] = BMLagrangeInterpolation_processOneSample(&This->lgInterpolation, tempBuffer, *strideIdx, inputLength + LGI_Order);
//
//                    if(i==*samplesToReachTarget&&*samplesToReachTarget!=samplesProcessing){
//                        //Reach the desiredDS
//                        retainInputIdx = roundf(*strideIdx);
//                        sampleToConsume = retainInputIdx + 1 - baseIdx;
//                        *delaySamples += i - sampleToConsume + 1;
////                        printf("reached target %f %d %f %zu %f %zu\n",*strideIdx,retainInputIdx,speed,inputLength,*delaySamples,desiredDS);
//
//                        *strideIdx = 0;
//                        reachTarget = true;
//                        *samplesToReachTarget = 0;
//                    }
//
//                    //possibility : when i==samplesProcess - 1 & reach target = true -> error
//                    if(!reachTarget){
//                        if(i==samplesProcessing-1){
//                            //End of loop
//                            sampleToConsume = floorf(*strideIdx) + 1;
//                            //Not reach target -> calculate stride idx
//                            *strideIdx -= floorf(*strideIdx);
//                            sampleToConsume -= baseIdx;
//                            *delaySamples += i - sampleToConsume + 1;
//                            *samplesToReachTarget -= samplesProcessing;
////                            printf("s %zu\n",*samplesToReachTarget);
//                        }else{
//                            *strideIdx += speed;
//                        }
//                    }
//                }else{
//                    //DelayTime reached target -> switch to Normal mode
//                    retainInputIdx++;
//                    sampleToConsume++;
//                    outBuffer[i+samplesProcessed] = tempBuffer[retainInputIdx];
////                    if(i==samplesProcessing-1){
////                        //End
////                        printf("idx %f %f %f\n",retainInputIdx-baseIdx,tempBuffer[retainInputIdx],sampleToConsume);
////                    }
//                }
//
//
//                if(i>0&&fabsf(outBuffer[i+samplesProcessed] - outBuffer[i+samplesProcessed-1])>0.2){
//                    printf("error %d %f\n",i,*strideIdx);
//                    int j =0;
//                    j++;
//                }
//            }
//        }else{
//            // copy from buffer tail to output
//            //copy 5 samples from buffer
//            size_t samplesFromLastFrame = 5;
//            memcpy(outBuffer+samplesProcessed, tempBuffer+samplesFromLastFrame, sizeof(float)*samplesFromLastFrame);
//            //Copy the rest
//            memcpy(outBuffer+samplesProcessed + samplesFromLastFrame, tail, byteProcessing-sizeof(float)*samplesFromLastFrame);
//
//            sampleToConsume = samplesProcessing;
//        }
//
////        printf("%f\n",sampleToConsume);
//        //Save 10 last samples into lgiUpBuffer 10 first samples
//        int samplesToStoreFromTail = sampleToConsume - LGI_Order;
//        memcpy(tempBuffer, tail  + samplesToStoreFromTail, sizeof(float)*LGI_Order);
//
//        // mark the read bytes as used
//        TPCircularBufferConsume(circularBuffer, BM_MIN(sampleToConsume*sizeof(float),bytesAvailableForRead));
//
////        float* nexttail = TPCircularBufferTail(circularBuffer, &bytesAvailableForRead);
////        printf("next %f %lu\n",*delaySamples,bytesAvailableForRead/sizeof(float));
//
//        samplesProcessed += samplesProcessing;
//    }
//}


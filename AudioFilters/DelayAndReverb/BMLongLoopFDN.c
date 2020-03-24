//
//  BMLongLoopFDN.c
//  AUCloudReverb
//
//  Created by hans anderson on 3/20/20.
//  This file is in the public domain.
//

#include "BMLongLoopFDN.h"
#include "BMReverb.h"
#include "BMIntegerMath.h"

#define BMLLFDN_MAX_BLOCK_SIZE 8

void BMLongLoopFDN_randomShuffle(bool* A, size_t length);

void BMLongLoopFDN_init(BMLongLoopFDN *This,
						size_t numDelays,
						float minDelaySeconds,
						float maxDelaySeconds,
						bool hasZeroTaps,
						size_t blockSize,
						size_t feedbackShiftByBlock,
						float sampleRate){
	// require block size to be a power of two
	assert(isPowerOfTwo(blockSize));
	// requre block size to be within the max size limit
	assert(1 <= blockSize && blockSize <= BMLLFDN_MAX_BLOCK_SIZE);
	// block size can not exceed network size
	assert(blockSize <= numDelays);
	// require an even number of delays
	assert(numDelays % 2 == 0);
	// numDelays must be divisible by blockSize
	assert(numDelays % blockSize == 0);
	
	This->blockSize = blockSize;
	This->feedbackShiftByDelay = feedbackShiftByBlock * blockSize;
	
	// set the matrix attenuation and inverse attenuation
	This->matrixAttenuation = sqrt(1.0f / (float)blockSize);
	This->inverseMatrixAttenuation = 1.0f / This->matrixAttenuation;
	
	This->numDelays = numDelays;
	This->hasZeroTaps = hasZeroTaps;
	
	// get the lengths of the longest and shortest delays in samples
	size_t minDelaySamples = minDelaySeconds * sampleRate;
	size_t maxDelaySamples = maxDelaySeconds * sampleRate;
	This->minDelaySamples = minDelaySamples;
	
	// generate random delay times
	size_t *delayLengths = malloc(sizeof(size_t)*numDelays);
	BMReverbRandomsInRange(minDelaySamples, maxDelaySamples, delayLengths, numDelays);
	
	// confirm that the delay times are sorted
	bool sorted = true;
	for(size_t i=1; i<numDelays; i++)
		if(delayLengths[i]<delayLengths[i-1]) sorted = false;
	assert(sorted);
	
	// assign the delay times evenly between L and R channels so that each
	// channel gets some short ones and some long ones
	size_t *temp = malloc(sizeof(size_t)*numDelays);
	memcpy(temp,delayLengths,sizeof(size_t)*numDelays);
//	size_t j=0;
//	for(size_t i=0; i<numDelays; i+= 2){
//		delayLengths[j] = temp[i];
//		delayLengths[j+numDelays/2] = temp[i + 1];
//		j++;
//	}
	for(size_t i=0; i<numDelays; i++)
		delayLengths[i] = temp[(i*39)%This->numDelays];
	
	This->mixBuffers = malloc(sizeof(float*) * This->numDelays);
	This->mixBuffers[0] = malloc(sizeof(float) * This->numDelays * minDelaySamples);
	for(size_t i=1; i<numDelays; i++)
		This->mixBuffers[i] = This->mixBuffers[0] + i * minDelaySamples;
	
	// record delay times
	This->delayTimes = malloc(sizeof(float)*numDelays);
	for(size_t i=0; i<numDelays; i++)
		This->delayTimes[i] = (float)delayLengths[i] / sampleRate;
	
	// init the input buffers
	This->inputBufferL = malloc(sizeof(float) * minDelaySamples * 2); //* 4);
	This->inputBufferR = This->inputBufferL + minDelaySamples;
//	This->delayBufferL = This->inputBufferR + minDelaySamples;
//	This->delayBufferR = This->delayBufferL + minDelaySamples;
	
	// init the delay buffers
	This->delays = malloc(numDelays * sizeof(TPCircularBuffer));
	for(size_t i=0; i<numDelays; i++){
		TPCircularBufferInit(&This->delays[i], ((uint32_t)delayLengths[i] + (uint32_t)minDelaySamples) * sizeof(float));
	}
	
	// write zeros into the head of each delay buffer to advance it to the proper delay time
	float *writePointer;
	uint32_t bytesAvailable;
	for(size_t i=0; i<numDelays; i++){
		writePointer = TPCircularBufferHead(&This->delays[i], &bytesAvailable);
		vDSP_vclr(writePointer, 1, delayLengths[i]);
		TPCircularBufferProduce(&This->delays[i], (uint32_t)delayLengths[i] * sizeof(float));
	}
	
//	// init the offset delays
//	This->offsetDelays = malloc(sizeof(BMShortSimpleDelay)*This->numDelays);
//	for(size_t i=0; i<numDelays; i++)
//		BMShortSimpleDelay_init(&This->offsetDelays[i], 1, i*5);
	
	// init the arrays of read and write pointers
	This->readPointers = malloc(sizeof(float*) * numDelays);
	This->writePointers = malloc(sizeof(float*) * numDelays);
	
	// how much does the input have to be attenuated to keep unity gain at the output?
	size_t numInputsPerChannelWithZeroTap;
	if(This->feedbackShiftByDelay > 0)
		numInputsPerChannelWithZeroTap = This->blockSize + (hasZeroTaps ? 1 : 0);
	else
		numInputsPerChannelWithZeroTap = This->numDelays/2 + (hasZeroTaps ? 1 : 0);
	This->inputAttenuation = sqrt(1.0f / (float)numInputsPerChannelWithZeroTap);
	
	// init the feedback coefficients
	float defaultDecayTime = 100.0f;
	This->feedbackCoefficients = malloc(sizeof(float)*numDelays);
	BMLongLoopFDN_setRT60Decay(This, defaultDecayTime);
	
	// set output tap signs
	This->tapSigns = malloc(numDelays * sizeof(bool));
	for(size_t i=0; i<numDelays; i++)
		This->tapSigns[i] = i % 2 == 0 ? false : true;
	
	// randomise the order of the output tap signs in each channel
	BMLongLoopFDN_randomShuffle(This->tapSigns, numDelays/2);
	BMLongLoopFDN_randomShuffle(This->tapSigns + numDelays/2, numDelays/2);
	
	This->inputPan = 0.2;
	
	free(delayLengths);
	delayLengths = NULL;
	free(temp);
	temp = NULL;
}





void BMLongLoopFDN_randomShuffle(bool* A, size_t length){
	// swap every element in A with another element in a random position
	for(size_t i=0; i<length; i++){
		size_t randomIndex = arc4random() % length;
		float temp = A[i];
		A[i] = A[randomIndex];
		A[randomIndex] = temp;
	}
}





void BMLongLoopFDN_free(BMLongLoopFDN *This){
	for(size_t i=0; i<This->numDelays; i++)
		TPCircularBufferCleanup(&This->delays[i]);
	free(This->delays);
	This->delays = NULL;
	
	free(This->feedbackCoefficients);
	This->feedbackCoefficients = NULL;
	
	free(This->delayTimes);
	This->delayTimes = NULL;
	
	free(This->tapSigns);
	This->tapSigns = NULL;
	
	free(This->readPointers);
	This->readPointers = NULL;
	
	free(This->writePointers);
	This->writePointers = NULL;
	
	free(This->inputBufferL);
	This->inputBufferL = NULL;
	
	free(This->mixBuffers[0]);
	free(This->mixBuffers);
	This->mixBuffers = NULL;
}





void BMLongLoopFDN_setRT60Decay(BMLongLoopFDN *This, float timeSeconds){
	for(size_t i=0; i<This->numDelays; i++){
		This->feedbackCoefficients[i] = This->matrixAttenuation * BMReverbDelayGainFromRT60(timeSeconds, This->delayTimes[i]);
	}
	
//	// prototype
//	for(size_t i=0; i<This->numDelays; i++){
//		This->feedbackCoefficients[i] =  BMReverbDelayGainFromRT60(timeSeconds, This->delayTimes[i]);
//	}
}




void BMLongLoopFDN_setInputPan(BMLongLoopFDN *This, float pan01){
	assert(0.0f <= pan01 && pan01 <= 1.0f);
	This->inputPan = pan01;
}


void BMLongLoopFDN_FHTStage1(float **in, float **out, size_t samplesProcessing){
		vDSP_vadd(in[0],1,in[1],1,out[0],1,samplesProcessing);
		vDSP_vsub(in[1],1,in[0],1,out[1],1,samplesProcessing);
}

void BMLongLoopFDN_FHTStage2(float **in, float **out, size_t samplesProcessing){
		vDSP_vadd(in[0],1,in[2],1,out[0],1,samplesProcessing);
		vDSP_vadd(in[1],1,in[3],1,out[1],1,samplesProcessing);
		vDSP_vsub(in[2],1,in[0],1,out[2],1,samplesProcessing);
		vDSP_vsub(in[3],1,in[1],1,out[3],1,samplesProcessing);
}

void BMLongLoopFDN_FHTStage3(float **in, float **out, size_t samplesProcessing){
		vDSP_vadd(in[0],1,in[4],1,out[0],1,samplesProcessing);
		vDSP_vadd(in[1],1,in[5],1,out[1],1,samplesProcessing);
		vDSP_vadd(in[2],1,in[6],1,out[2],1,samplesProcessing);
		vDSP_vadd(in[3],1,in[7],1,out[3],1,samplesProcessing);
		vDSP_vsub(in[4],1,in[0],1,out[4],1,samplesProcessing);
		vDSP_vsub(in[5],1,in[1],1,out[5],1,samplesProcessing);
		vDSP_vsub(in[6],1,in[2],1,out[6],1,samplesProcessing);
		vDSP_vsub(in[7],1,in[3],1,out[7],1,samplesProcessing);
}


void BMLongLoopFDN_process(BMLongLoopFDN *This,
						   const float* inputL, const float* inputR,
						   float *outputL, float *outputR,
						   size_t numSamples){
	
	// limit the input to chunks of size <= minDelayLength
	while(numSamples > 0){
		size_t samplesProcessing = BM_MIN(numSamples, This->minDelaySamples);
		
		
		// attenuate the input to keep the volume unitary between input and output and cache to buffers
		float leftAttenuation = This->inputAttenuation * sqrt((1.0 - This->inputPan) * 2.0);
		vDSP_vsmul(inputL, 1, &leftAttenuation, This->inputBufferL, 1, samplesProcessing);
		float rightAttenuation = This->inputAttenuation * sqrt(This->inputPan * 2.0);
		vDSP_vsmul(inputR, 1, &rightAttenuation, This->inputBufferR, 1, samplesProcessing);
		
		
		// set the outputs to zero
		vDSP_vclr(outputL, 1, samplesProcessing);
		vDSP_vclr(outputR, 1, samplesProcessing);
		
		
		// get read pointers for each delay and limit the number of samples processing
		// according to what's available in the delays
		for(size_t i=0; i<This->numDelays; i++){
			uint32_t bytesAvailable;
			uint32_t bytesProcessing = (uint32_t)sizeof(float)*(uint32_t)samplesProcessing;
			// get a read pointer
			This->readPointers[i] = TPCircularBufferTail(&This->delays[i], &bytesAvailable);
			// reduce bytesProcessing to not exceed bytesAvailable
			bytesProcessing = MIN(bytesProcessing,bytesAvailable);
			// get a write pointer
			This->writePointers[i] = TPCircularBufferHead(&This->delays[i], &bytesAvailable);
			// reduce bytesProcessing to not exceed bytesAvailable
			bytesProcessing = MIN(bytesProcessing,bytesAvailable);
			
			// reduce samples processing if the requested number of samples in unavailable
			samplesProcessing = bytesProcessing / sizeof(float);
			
			if(samplesProcessing < This->minDelaySamples && numSamples > samplesProcessing)
				printf("processing short: %zu\n", samplesProcessing);
		}
		
		
		// 1. attenuate the output signal according to the RT60 decay time and the
		// mixing matrix attenuation
		// 2. mix output to outputL and outputR
		for(size_t i=0; i<This->numDelays; i++){
			// attenuate the data at the read pointers to get desired decay time
			vDSP_vsmul(This->readPointers[i], 1, &This->feedbackCoefficients[i], This->readPointers[i], 1, samplesProcessing);
		
			// we will write the first half the delays to the left output and the second half to the right output
			float *outputPointer = (i < This->numDelays/2) ? outputL : outputR;
			// output mix: add the ith delay to the output if its tap sign is positive
			if(This->tapSigns[i])
				vDSP_vadd(outputPointer, 1, This->readPointers[i], 1, outputPointer, 1, samplesProcessing);
			// or subtract it if negative
			else
				vDSP_vsub(outputPointer, 1, This->readPointers[i], 1, outputPointer, 1, samplesProcessing);
		}
		
	
		// remove the matrix attenuation from the outputs by dividing it out
		vDSP_vsmul(outputL, 1, &This->inverseMatrixAttenuation, outputL, 1, samplesProcessing);
		vDSP_vsmul(outputR, 1, &This->inverseMatrixAttenuation, outputR, 1, samplesProcessing);
		
		
		// mix the zero taps to the output if we have them
		if(This->hasZeroTaps){
			vDSP_vadd(This->inputBufferL, 1, outputL, 1, outputL, 1, samplesProcessing);
			vDSP_vadd(This->inputBufferR, 1, outputR, 1, outputR, 1, samplesProcessing);
		}
		
		
		// rename some buffers to shorten the code in the following sections:
		float **wp, **rp, **mb;
		wp = This->writePointers;
		rp = This->readPointers;
		mb = This->mixBuffers;
		
		
		// apply the mixing matrix and write to write pointers
		if(This->blockSize == 1){
			for(size_t i=0; i<This->numDelays; i++){
				size_t shift = (i+This->feedbackShiftByDelay) % This->numDelays;
				memcpy(wp[shift],rp[i],sizeof(float)*samplesProcessing);
			}
		}
		if(This->blockSize == 2){
			for(size_t i=0; i<This->numDelays; i+=2){
				size_t shift = (i+This->feedbackShiftByDelay) % This->numDelays;
				// fast hadamard transform stage 1
				BMLongLoopFDN_FHTStage1(rp+i,wp+shift, samplesProcessing);
			}
		}
		if(This->blockSize == 4){
			for(size_t i=0; i<This->numDelays; i+=4){
				// fast hadamard transform stage 1
				for(size_t j=0; j<4; j+=2)
					BMLongLoopFDN_FHTStage1(rp+i+j, mb+i+j, samplesProcessing);
				
				// fast hadamard transform stage 2 with rotation
				size_t shift = (i+This->feedbackShiftByDelay) % This->numDelays;
				BMLongLoopFDN_FHTStage2(mb+i, wp+shift, samplesProcessing);
			}
		}
		if(This->blockSize == 8){
			for(size_t i=0; i<This->numDelays; i+=8){
				// fast hadamard transform stage 1
				for(size_t j=0; j<8; j+=2)
					BMLongLoopFDN_FHTStage1(rp+i+j, wp+i+j, samplesProcessing);
				
				// fast hadamard transform stage 2
				for(size_t j=0; j<8; j+=4)
					BMLongLoopFDN_FHTStage2(wp+i+j, mb+i+j, samplesProcessing);
				
				// fast hadamard transform stage 3 with rotation
				size_t shift = (i + This->feedbackShiftByDelay) % This->numDelays;
				BMLongLoopFDN_FHTStage3(mb+i, wp+shift, samplesProcessing); 	 		
			}
		}
		

		// mix inputs with feedback signals and write back into the delays
		uint32_t bytesProcessing = (uint32_t)samplesProcessing * sizeof(float);
		for(size_t i=0; i<This->numDelays; i++){
			// if we are circulating by block, input only to the first block
			if(This->feedbackShiftByDelay > 0){
				// mix the inputL and inputR into the delay inputs
				if(i<This->blockSize){
//					// apply a micro-delay to prevent outputs from coinciding
//					const float *inputs [1] = {This->inputBufferL};
//					float *outputs [1] = {This->delayBufferL};
//					BMShortSimpleDelay_process(&This->offsetDelays[i], inputs, outputs, 1, samplesProcessing);
					// mix the input
					vDSP_vadd(This->writePointers[i], 1, This->inputBufferL, 1, This->writePointers[i], 1, samplesProcessing);
				}
				else if(This->numDelays/2 <= i && i < This->numDelays/2 + This->blockSize){
//					// apply a micro-delay to prevent outputs from coinciding
//					const float *inputs [1] = {This->inputBufferR};
//					float *outputs [1] = {This->delayBufferR};
//					BMShortSimpleDelay_process(&This->offsetDelays[i], inputs, outputs, 1, samplesProcessing);
					// mix the input
					vDSP_vadd(This->writePointers[i], 1, This->inputBufferR, 1, This->writePointers[i], 1, samplesProcessing);
				}
			}
			// if each block feeds back to itself, input to all blocks
			else {
				// mix the inputL and inputR into the delay inputs
				if(i<This->numDelays/2){
					// mix the input
					vDSP_vadd(This->writePointers[i], 1, This->inputBufferL, 1, This->writePointers[i], 1, samplesProcessing);
				}
				else {
					// mix the input
					vDSP_vadd(This->writePointers[i], 1, This->inputBufferR, 1, This->writePointers[i], 1, samplesProcessing);
				}
			}
			
			// mark the delays read
			TPCircularBufferConsume(&This->delays[i], bytesProcessing);
		
			// mark the delays written
			TPCircularBufferProduce(&This->delays[i], bytesProcessing);
		}
		
		
		// advance pointers
		numSamples -= samplesProcessing;
		inputL  += samplesProcessing;
		inputR  += samplesProcessing;
		outputL += samplesProcessing;
		outputR += samplesProcessing;
	}
}


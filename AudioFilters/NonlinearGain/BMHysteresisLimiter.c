//
//  BMHysteresisLimiter.c
//  AudioFiltersXcodeProject
//
//  Created by Hans on 29/10/19.
//  Anyone may use this file without restrictions
//

#include "BMHysteresisLimiter.h"
#include "BMAsymptoticLimiter.h"
#include <math.h>
#include <assert.h>
#include "Constants.h"

#define BM_HYSTERESISLIMITER_DEFAULT_POWER_LIMIT -45.0f


void BMHysteresisLimiter_processMonoRectifiedSimple(BMHysteresisLimiter *This,
                                      const float *inputPos, const float *inputNeg,
                                      float* outputPos, float* outputNeg,
                                      size_t numSamples){
	// create two alias pointers for code readability
	float* limitedPos = outputPos;
	float* limitedNeg = outputNeg;
	
	// apply asymptotic limit
	BMAsymptoticLimitRectified(inputPos, inputNeg,
							   limitedPos, limitedNeg,
							   numSamples);
	
    for(size_t i=0; i<numSamples; i++){
		// positive output
		float oPos = limitedPos[i] * This->c;
		// update charge
		This->c -= oPos;
		This->c += This->halfR*(1.0f - This->c);
		// negative output
        float oNeg = limitedNeg[i] * This->c;
        // update charge
		This->c += oNeg;
		This->c += This->halfR*(1.0f - This->c);
		// output
        outputPos[i] = oPos*This->oneOverR;
        outputNeg[i] = oNeg*This->oneOverR;
    }
}



void BMHysteresisLimiter_processStereoRectifiedSimple(BMHysteresisLimiter *This,
													  const float *inputPosL,
													  const float *inputPosR,
													  const float *inputNegL,
													  const float *inputNegR,
													  float *outputPosL, float *outputPosR,
													  float *outputNegL, float *outputNegR,
													  size_t numSamples){
	// create some alias pointers for code readability
	float* limitedPosL = outputPosL;
	float* limitedNegL = outputNegL;
	float* limitedPosR = outputPosR;
	float* limitedNegR = outputNegR;
	
	// apply asymptotic limit
	BMAsymptoticLimitRectified(inputPosL, inputNegL,
							   limitedPosL, limitedNegL,
							   numSamples);
	BMAsymptoticLimitRectified(inputPosR, inputNegR,
							   limitedPosR, limitedNegR,
							   numSamples);
	
    for(size_t i=0; i<numSamples; i++){
		// positive output
		simd_float2 iPos = simd_make_float2(limitedPosL[i], limitedPosR[i]);
		simd_float2 oPos = iPos * This->cs;
		// update charge
		This->cs -= oPos;
		This->cs += This->halfR * (1.0f - This->cs);
		// negative output
		simd_float2 iNeg = simd_make_float2(limitedNegL[i], limitedNegR[i]);
		simd_float2 oNeg = iNeg * This->cs;
        // update charge
		This->cs += oNeg;
		This->cs += This->halfR * (1.0f - This->cs);
		// scale to compensate for gain loss
		oPos *= This->oneOverR;
		oNeg *= This->oneOverR;
		// output
        outputPosL[i] = oPos.x;
		outputPosR[i] = oPos.y;
        outputNegL[i] = oNeg.x;
		outputNegR[i] = oNeg.y;
    }
}



void BMHysteresisLimiter_processStereoSimple(BMHysteresisLimiter *This,
											 const float *inputL, const float *inputR,
											 float *outputL, float *outputR,
											 size_t numSamples){
	// create some aliases for code readability
	float *limitedL = outputL;
	float *limitedR = outputR;
	
	// apply asymptotic limit
	BMAsymptoticLimit(inputL, limitedL, numSamples);
	BMAsymptoticLimit(inputR, limitedR, numSamples);
	
	for(size_t i=0; i<numSamples; i++){
		// output
		simd_float2 in = simd_make_float2(limitedL[i], limitedR[i]);
		simd_float2 out = in * This->cs;
		// update charge
		This->cs -= simd_abs(out);
		This->cs += This->halfR * (1.0f - This->cs);
		// scale to compensate for gain loss
		out *= This->oneOverR;
		// output
		outputL[i] = out.x;
		outputR[i] = out.y;
	}
}



void BMHysteresisLimiter_setPowerLimit(BMHysteresisLimiter *This, float limitDb){
	assert(limitDb <= 0.0f);
	float limit01 = BM_DB_TO_GAIN(limitDb);
	
	This->R = limit01;
	This->halfR = This->R * 0.5;
	This->oneOverR = 1.0 / limit01;
}


void BMHysteresisLimiter_init(BMHysteresisLimiter *This){
	
	BMHysteresisLimiter_setPowerLimit(This, BM_HYSTERESISLIMITER_DEFAULT_POWER_LIMIT);
	
	This->c = 0.0f;
	This->cs = 0.0f;
}

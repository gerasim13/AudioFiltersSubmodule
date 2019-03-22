//
//  BMLagrangeInterpolation.h
//  BMAudioFilters
//
//  Created by Nguyen Minh Tien on 12/26/18.
//  Copyright © 2018 Hans. All rights reserved.
//

#ifndef BMLagrangeInterpolation_h
#define BMLagrangeInterpolation_h

#include <stdio.h>

typedef struct BMLagrangeInterpolation {
    float** table;
    size_t order;
    size_t deltaRange;
    size_t sampleRange;
    float* temp;
    float deltaStep;
    float totalStepPerSample;
    
}BMLagrangeInterpolation;

void BMLagrangeInterpolation_init(BMLagrangeInterpolation* This, int order);
/*
Provide upsample output. The number of upsample base on the order.
 2nd order will provide double samples mean double sampleRate also.
 */
void BMLagrangeInterpolation_processUpSample(BMLagrangeInterpolation* This, const float* input, const float* strideInput, float* output, int outputStride,size_t inputLength,size_t length);

/*
 Down sample base on the order. Process this to get the sample back after upsample
 */
void BMLagrangeInterpolation_processDownSample(BMLagrangeInterpolation* This, const float* input,int inputStride, float* output, int outputStride,size_t inputLength,size_t outputLength);

#endif /* BMLagrangeInterpolation_h */

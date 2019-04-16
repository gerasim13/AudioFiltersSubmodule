//
//  BMMultiLevelBiquad.h
//  VelocityFilter
//
//  Created by Hans on 14/3/16.
//
//  This file may be used, distributed and modified freely by anyone,
//  for any purpose, without restrictions.
//

#ifndef BMMultiLevelBiquad_h
#define BMMultiLevelBiquad_h

#include <stdio.h>
#include <Accelerate/Accelerate.h>

#ifdef __cplusplus
extern "C" {
#endif
    
    typedef struct BMMultiLevelBiquad {
        // dynamic memory
        vDSP_biquadm_Setup multiChannelFilterSetup;
        vDSP_biquad_Setup singleChannelFilterSetup;
        float* monoDelays;
        double* coefficients_d;
        float* coefficients_f;
        
        // static memory
        float currentGain;
        float desiredGain;
        size_t numLevels;
        size_t numChannels;
        double sampleRate;
        bool needsUpdate, useRealTimeUpdate, useBiquadm,useSmoothUpdate,needUpdateActiveLevels;
        bool *activeLevels;
    } BMMultiLevelBiquad;
    
    
    
    // process a stereo buffer of samples
    void BMMultiLevelBiquad_processBufferStereo(BMMultiLevelBiquad* bqf, const float* inL, const float* inR, float* outL, float* outR, size_t numSamples);
    
    // process a mono buffer of samples
    void BMMultiLevelBiquad_processBufferMono(BMMultiLevelBiquad* bqf, const float* input, float* output, size_t numSamples);
    
    /*
     * init must be called once before using the filter.  To change the number of
     * levels in the fitler, call destroy first, then call this function with
     * the new number of levels
     *
     * monoRealTimeUpdate: If you are updating coefficients of a MONO filter in
     *                     realtime, set this to true. Processing of audio is
     *                     slightly slower, but updates can happen in realtime.
     *                     This setting has no effect on stereo filters.
     *                     This setting has no effect if the OS does not support
     *                     realtime updates of vDSP_biquadm filter coefficients.
     *
     *smoothUpdate :    When BMMultilevelBiquad is init with smooth updates on,
     *                  the update function will call setTargetsDouble to enable smooth update;
     *                  and when it's off it will call setCoefficientsDouble
     *
     */
    void BMMultiLevelBiquad_init(BMMultiLevelBiquad* bqf,
                                 size_t numLevels,
                                 float sampleRate,
                                 bool isStereo,
                                 bool monoRealTimeUpdate,
                                 bool smoothUpdate);
    
    // free up memory objects
    void BMMultiLevelBiquad_destroy(BMMultiLevelBiquad* bqf);
    
    
    // set a bell-shape filter at on the specified level in both channels
    // and update filter settings
    void BMMultiLevelBiquad_setBell(BMMultiLevelBiquad* bqf, float fc, float bandwidth, float gain_db, size_t level);
    
    /*!
     * BMMultiLevelBiquad_setNormalizedBell
     *
     * @abstract This is based on the setBell function above. The difference is that it attempts to keep the overall gain at unity by adjusting the broadband gain to compensate for the boost or cut of the bell filter. This allows us to acheive extreme filter curves that approach the behavior of bandpas and notch filters without clipping or loosing too much signal gain.
     * @param bqf pointer to an initialized struct
     * @param fc bell filter cutoff frequency in Hz
     * @param bandwidth bell filter bandwidth in Hz
     * @param controlGainDb the actual post-compensation gain of the filter at fc
     * @param level the level number in the multilevel biquad struct
     */
    void BMMultiLevelBiquad_normalizedBell(BMMultiLevelBiquad* bqf, float fc, float bandwidth, float controlGainDb, size_t level);
    
    
    // set a high shelf filter at on the specified level in both
    // channels and update filter settings
    void BMMultiLevelBiquad_setHighShelf(BMMultiLevelBiquad* bqf, float fc, float gain_db, size_t level);
    
    
    // set a low shelf filter at on the specified level in both
    // channels and update filter settings
    void BMMultiLevelBiquad_setLowShelf(BMMultiLevelBiquad* bqf, float fc, float gain_db, size_t level);
    
    void BMMultiLevelBiquad_setLowPass12db(BMMultiLevelBiquad* bqf, double fc, size_t level);
    
    void BMMultiLevelBiquad_setLowPassQ12db(BMMultiLevelBiquad* bqf, double fc,double q, size_t level);
    
    void BMMultiLevelBiquad_setHighPass12db(BMMultiLevelBiquad* bqf, double fc, size_t level);
    
    void BMMultiLevelBiquad_setHighPassQ12db(BMMultiLevelBiquad* bqf, double fc,double q,size_t level);
    
    /*
     * sets up a butterworth filter of order 2*numLevels
     *
     * @param firstLevel   the filter will consume numLevels contiguous
     *                     biquad sections, beginning with firstLevel
     * @param numLevels    number of biquad sections to use (order / 2)
     */
    void BMMultiLevelBiquad_setHighOrderBWLP(BMMultiLevelBiquad* bqf, double fc, size_t firstLevel, size_t numLevels);
    
    
    
    
    
    
    /*!
     * BMMultiLevelBiquad_setLegendreLP
     *
     * @abstract sets up a Legendre Lowpass filter of order 2*numLevels
     * @discussion Legendre lowpass fitlers have steeper cutoff than Butterworth, with nearly flat passband
     *
     * @param fc           cutoff frequency
     * @param firstLevel   the filter will consume numLevels contiguous
     *                     biquad sections, beginning with firstLevel
     * @param numLevels    number of biquad sections to use. numLevels = (filterOrder / 2)
     */
    void BMMultiLevelBiquad_setLegendreLP(BMMultiLevelBiquad* bqf, double fc, size_t firstLevel, size_t numLevels);
    
    
    
    
    
    
    /*!
     * BMMultiLevelBiquad_setCriticallyDampedLP
     *
     * @abstract sets up a Critically-damped Lowpass filter of order 2*numLevels
     *
     * @param fc           cutoff frequency
     * @param firstLevel   the filter will consume numLevels contiguous
     *                     biquad sections, beginning with firstLevel
     * @param numLevels    number of biquad sections to use. numLevels = (filterOrder / 2)
     */
    void BMMultiLevelBiquad_setCriticallyDampedLP(BMMultiLevelBiquad* bqf, double fc, size_t firstLevel, size_t numLevels);
    
    
    
    
    
    /*!
     * BMMultiLevelBiquad_setBesselLP
     *
     * @abstract sets up a Bessel Lowpass filter of order 2*numLevels
     * @discussion Bessel lowpass filters have less steep cutoff than Butterworth but they have nearly constant group delay in the passband and their step response is almost critically damped.
     *
     * @param fc           cutoff frequency
     * @param firstLevel   the filter will consume numLevels contiguous
     *                     biquad sections, beginning with firstLevel
     * @param numLevels    number of biquad sections to use. numLevels = (filterOrder / 2)
     */
    void BMMultiLevelBiquad_setBesselLP(BMMultiLevelBiquad* bqf, double fc, size_t firstLevel, size_t numLevels);
    
    void BMMultiLevelBiquad_setLowPass6db(BMMultiLevelBiquad* bqf, double fc, size_t level);
    
    void BMMultiLevelBiquad_setHighPass6db(BMMultiLevelBiquad* bqf, double fc, size_t level);
    
    
    void BMMultiLevelBiquad_setLinkwitzRileyLP(BMMultiLevelBiquad* bqf, double fc, size_t level);
    
    
    void BMMultiLevelBiquad_setLinkwitzRileyHP(BMMultiLevelBiquad* bqf, double fc, size_t level);
    
    
    // Calling this sets the filter coefficients at 'level' to bypass.
    // Note that the filter still processes through the bypassed section
    // but the output is the same as the input.
    void BMMultiLevelBiquad_setBypass(BMMultiLevelBiquad* bqf, size_t level);
    
    
    // set gain in db
    void BMMultiLevelBiquad_setGain(BMMultiLevelBiquad* bqf, float gain_db);
    
    
    /*!
     * BMMultiLevelBiquad_tfMagVector
     *
     * @param frequency: an an array specifying frequencies at which we want to evaluate
     * the transfer function magnitude of the filter
     *
     * @param magnitude: an array for storing the result
     * @param length: the number of elements in frequency and magnitude
     *
     */
    void BMMultiLevelBiquad_tfMagVector(BMMultiLevelBiquad* bqf, const float *frequency, float *magnitude, size_t length);
    void BMMultiLevelBiquad_tfMagVectorAtLevel(BMMultiLevelBiquad* bqf, const float *frequency, float *magnitude, size_t length,size_t level);
    
    /*!
     * BMMultiLevelBiquad_groupDelay
     *
     * returns the total group delay of all levels of the filter at the specified frequency.
     *
     * @discussion uses a cookbook formula for group delay of biquad filters, based on the fft derivative method.
     *
     * @param freq the frequency at which you need to compute the group delay of the filter cascade
     * @return the group delay in samples at freq
     */
    double BMMultiLevelBiquad_groupDelay(BMMultiLevelBiquad* bqf, double freq);
    
    //Call this function to manually disable desired filter level
    void BMMultiLevelBiquad_setActiveOnLevel(BMMultiLevelBiquad* bqf,bool active,size_t level);
    
#ifdef __cplusplus
}
#endif

#endif /* BMMultiLevelBiquad_h */

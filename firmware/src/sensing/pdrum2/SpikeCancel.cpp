#include "SpikeCancel.h"

// Ported from Edrumulus_hardware::cancel_ADC_spikes (Volker Fischer, GPL-2.0),
// collapsed to a single channel. Logic preserved 1:1; only the per-pad/per-input
// array indexing was removed in favour of plain members.

void SpikeCancel::reset() {
    prev1State_ = prev2State_ = prev3State_ = prev4State_ = prev5State_ = ST_NOISE;
    prevInput1_ = prevInput2_ = prevInput3_ = prevInput4_ = 0.0f;
    prevOverload1_ = prevOverload2_ = prevOverload3_ = prevOverload4_ = 0;
}

void SpikeCancel::process(float& signal, int& overloadDetected, int level) {
    // remove single/dual sample spikes by checking if right before and right after
    // the detected spike(s) we only have noise and no useful signal (the ESP32
    // spikes mostly are on just one or two sample(s))
    const float signal_org            = signal;
    signal                            = prevInput4_;     // normal return: 4-sample-delayed value
    const int   overload_detected_org = overloadDetected;
    overloadDetected                  = prevOverload4_;
    const float input_abs             = fabsf ( signal_org );
    SpikeState  input_state           = ST_OTHER;

    if ( input_abs < kMaxNoiseAmpl )
    {
        input_state = ST_NOISE;
    }
    else if ( ( signal_org < kMaxPeakThresh ) && ( signal_org > 0 ) )
    {
        input_state = ST_SPIKE_HIGH;
    }
    else if ( ( signal_org > -kMaxPeakThresh ) && ( signal_org < 0 ) )
    {
        input_state = ST_SPIKE_LOW;
    }

    // check for single high spike sample case
    if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_LOW ) ) &&
         (   prev4State_ == ST_SPIKE_HIGH ) &&
         ( ( prev3State_ == ST_NOISE ) || ( prev3State_ == ST_SPIKE_LOW ) ) )
    {
        signal = 0.0f;
    }

    // check for single low spike sample case
    if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_HIGH ) ) &&
         (   prev4State_ == ST_SPIKE_LOW ) &&
         ( ( prev3State_ == ST_NOISE ) || ( prev3State_ == ST_SPIKE_HIGH ) ) )
    {
        signal = 0.0f;
    }

    if ( level >= 2 )
    {
        // two sample high spike case
        if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_LOW ) ) &&
             (   prev4State_ == ST_SPIKE_HIGH ) &&
             (   prev3State_ == ST_SPIKE_HIGH ) &&
             ( ( prev2State_ == ST_NOISE ) || ( prev2State_ == ST_SPIKE_LOW ) ) )
        {
            signal      = 0.0f;
            prevInput3_ = 0.0f;
        }

        // two sample low spike case
        if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_HIGH ) ) &&
             (   prev4State_ == ST_SPIKE_LOW ) &&
             (   prev3State_ == ST_SPIKE_LOW ) &&
             ( ( prev2State_ == ST_NOISE ) || ( prev2State_ == ST_SPIKE_HIGH ) ) )
        {
            signal      = 0.0f;
            prevInput3_ = 0.0f;
        }
    }

    if ( level >= 3 )
    {
        // three sample high spike case
        if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_LOW ) ) &&
             (   prev4State_ == ST_SPIKE_HIGH ) &&
             (   prev3State_ == ST_SPIKE_HIGH ) &&
             (   prev2State_ == ST_SPIKE_HIGH ) &&
             ( ( prev1State_ == ST_NOISE ) || ( prev1State_ == ST_SPIKE_LOW ) ) )
        {
            signal      = 0.0f;
            prevInput3_ = 0.0f;
            prevInput2_ = 0.0f;
        }

        // three sample low spike case
        if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_HIGH ) ) &&
             (   prev4State_ == ST_SPIKE_LOW ) &&
             (   prev3State_ == ST_SPIKE_LOW ) &&
             (   prev2State_ == ST_SPIKE_LOW ) &&
             ( ( prev1State_ == ST_NOISE ) || ( prev1State_ == ST_SPIKE_HIGH ) ) )
        {
            signal      = 0.0f;
            prevInput3_ = 0.0f;
            prevInput2_ = 0.0f;
        }
    }

    if ( level >= 4 )
    {
        // four sample high spike case
        if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_LOW ) ) &&
             (   prev4State_ == ST_SPIKE_HIGH ) &&
             (   prev3State_ == ST_SPIKE_HIGH ) &&
             (   prev2State_ == ST_SPIKE_HIGH ) &&
             (   prev1State_ == ST_SPIKE_HIGH ) &&
             ( ( input_state == ST_NOISE ) || ( input_state == ST_SPIKE_LOW ) ) )
        {
            signal      = 0.0f;
            prevInput3_ = 0.0f;
            prevInput2_ = 0.0f;
            prevInput1_ = 0.0f;
        }

        // four sample low spike case
        if ( ( ( prev5State_ == ST_NOISE ) || ( prev5State_ == ST_SPIKE_HIGH ) ) &&
             (   prev4State_ == ST_SPIKE_LOW ) &&
             (   prev3State_ == ST_SPIKE_LOW ) &&
             (   prev2State_ == ST_SPIKE_LOW ) &&
             (   prev1State_ == ST_SPIKE_LOW ) &&
             ( ( input_state == ST_NOISE ) || ( input_state == ST_SPIKE_HIGH ) ) )
        {
            signal      = 0.0f;
            prevInput3_ = 0.0f;
            prevInput2_ = 0.0f;
            prevInput1_ = 0.0f;
        }
    }

    // update five-step input state memory + four previous untouched input samples
    prev5State_    = prev4State_;
    prev4State_    = prev3State_;
    prev3State_    = prev2State_;
    prev2State_    = prev1State_;
    prevInput4_    = prevInput3_;
    prevInput3_    = prevInput2_;
    prevInput2_    = prevInput1_;
    prevOverload4_ = prevOverload3_;
    prevOverload3_ = prevOverload2_;
    prevOverload2_ = prevOverload1_;

    // adjust latency according to spike cancellation level
    if ( level >= 3 )
    {
        prev1State_    = input_state;
        prevInput1_    = signal_org;
        prevOverload1_ = overload_detected_org;
    }
    else if ( level >= 2 )
    {
        prev2State_    = input_state;
        prevInput2_    = signal_org;
        prevOverload2_ = overload_detected_org;
    }
    else
    {
        prev3State_    = input_state;
        prevInput3_    = signal_org;
        prevOverload3_ = overload_detected_org;
    }
}

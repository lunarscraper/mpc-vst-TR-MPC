/*
 * sc808_bd_punch.h — a second bass drum for 8W8: the "Punch" model.
 *
 * LOCAL ADDITION, not part of athousanddetails' schwung-8W8 (see VENDORED.md).
 *
 * The 808 kick is a bridged-T resonator: a long, round boom with almost no
 * pitch movement. A techno kick of the Jomox MBase / Alpha Base kind is built
 * the other way round, and that is what this voice does:
 *
 *   - ONE oscillator, phase-reset on every hit, so every hit is identical
 *   - a PITCH ENVELOPE falling from far above the note onto it — the punch
 *   - Harmonics: the sine bent towards a square (odd harmonics)
 *   - Pulse: the trigger click, and Noise: a short burst on the attack
 *   - Gate: the amplitude is held at full for this long before it decays
 *   - Comp: the amplitude envelope is compressed, so the body stays loud and
 *     then drops, instead of fading from the first millisecond
 *   - EQ: one knob, lowpass below its neutral point, treble lift above it
 *
 * It is a model of that ARCHITECTURE, not a circuit model of any Jomox
 * instrument, and it makes no claim to be one.
 *
 * The engine callocs its voices, so NO CONSTRUCTOR RUNS and no default member
 * initialiser can be relied on: init() sets every field.
 *
 * GPL-3.0.
 */
#ifndef SC808_BD_PUNCH_H
#define SC808_BD_PUNCH_H

#include <math.h>
#include <stdint.h>

namespace sc808 {

class PunchKick
{
public:
    void init(const double sampleRate)
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        active_ = false;
        phase_ = 0.0; hz_ = 50.0;
        pitchK_ = 0.0; penv_ = 0.0; pcoef_ = 0.0;
        env_ = 0.0; dcoef_ = 0.0; holdLeft_ = 0;
        atk_ = 0.0; atkStep_ = 1.0;
        snap_ = 0.0; snapCoef_ = 0.0; snapAmt_ = 0.0;
        harm_ = 0.0;
        pulse_ = 0.0; pulseCoef_ = 0.0;
        noise_ = 0.0; noiseCoef_ = 0.0; noiseLp_ = 0.0; noiseLpC_ = 0.0;
        thr_ = 1.0; invRatio_ = 1.0; tailGain_ = 1.0;
        lpOn_ = false; lpC_ = 1.0; lpZ_ = 0.0;
        shelfG_ = 0.0; shelfC_ = 0.0; shelfZ_ = 0.0;
        declick_ = 0.0; declickCoef_ = 0.0; last_ = 0.0;
        rng_ = 0x2545F491u;
    }

    /*
     * hz        the note the pitch envelope lands on
     * decayPos  0..1, amplitude decay: 30 ms .. 1.5 s to -60 dB
     * eqPos     0..1, neutral at 1/3 (the 808 Tone pot's default position)
     * attackPos 0..1, onset: soft and slow at 0, hard with an overshoot at 1
     * pitchSemi pitch envelope depth in semitones above the note
     * pitchSec  pitch envelope time constant, seconds
     * harm      0..1
     * pulse     0..1
     * noise     0..1
     * gateSec   hold time before the decay starts
     * comp      0..1
     */
    void trigger(const double hz, const float decayPos, const float eqPos,
                 const float attackPos, const float pitchSemi,
                 const float pitchSec, const float harm, const float pulse,
                 const float noise, const float gateSec, const float comp)
    {
        /* A hit on top of a ringing hit restarts the oscillator at phase 0.
         * The jump that leaves is faded over about a millisecond instead of
         * being left as a click the player did not ask for. */
        declick_     = active_ ? last_ : 0.0;
        declickCoef_ = exp(-1.0 / (0.0012 * sr_));

        hz_     = hz < 10.0 ? 10.0 : hz;
        phase_  = 0.0;

        /* pitch envelope, as an exponent: f = hz * exp(pitchK * penv) */
        pitchK_ = (double)clamp01(pitchSemi / 60.0f) * 60.0 * (M_LN2 / 12.0);
        penv_   = 1.0;
        pcoef_  = exp(-1.0 / ((pitchSec < 0.001f ? 0.001 : (double)pitchSec) * sr_));

        /* amplitude: hold, then exponential to -60 dB in T60 */
        const double t60 = 0.030 * pow(50.0, (double)clamp01(decayPos));
        dcoef_    = exp(-6.9078 / (t60 * sr_));
        env_      = 1.0;
        holdLeft_ = (int)((gateSec < 0.0f ? 0.0 : (double)gateSec) * sr_);

        /* onset ramp and overshoot */
        const double a  = (double)clamp01(attackPos);
        const double ia = 1.0 - a;
        const double ramp = 0.0001 + 0.0025 * ia * ia * ia;   /* 2.6 ms .. 0.1 ms */
        atk_      = 0.0;
        atkStep_  = 1.0 / (ramp * sr_);
        snapAmt_  = 1.2 * a;
        snap_     = 1.0;
        snapCoef_ = exp(-1.0 / (0.004 * sr_));

        harm_ = (double)clamp01(harm);

        pulse_     = (double)clamp01(pulse) * 0.9;
        pulseCoef_ = exp(-1.0 / (0.0005 * sr_));

        noise_     = (double)clamp01(noise) * 0.7;
        noiseCoef_ = exp(-1.0 / (0.006 * sr_));
        noiseLp_   = 0.0;
        noiseLpC_  = 1.0 - exp(-2.0 * M_PI * 1500.0 / sr_);

        /* compression of the amplitude envelope, peak-normalised:
         *   above the threshold  out = env^(1/ratio)
         *   below it             out = env * thr^(1/ratio - 1)
         * so the top is unchanged and the body comes up by up to 12 dB. */
        const double c = (double)clamp01(comp);
        thr_      = 1.0 - 0.8 * c;
        invRatio_ = 1.0 / (1.0 + 7.0 * c);
        tailGain_ = pow(thr_, invRatio_ - 1.0);

        /* EQ: one knob. Below 1/3 a lowpass closing to 250 Hz; above it a
         * treble lift of up to +12 dB over 1.2 kHz. */
        const double e = (double)clamp01(eqPos);
        const double third = 1.0 / 3.0;
        if(e < third - 0.01)
        {
            const double fc = 250.0 * pow(80.0, e / third);
            lpOn_ = true;
            lpC_  = 1.0 - exp(-2.0 * M_PI * fc / sr_);
        }
        else
        {
            lpOn_ = false;
        }
        shelfG_ = e > third + 0.01 ? 3.0 * (e - third) / (1.0 - third) : 0.0;
        shelfC_ = 1.0 - exp(-2.0 * M_PI * 1200.0 / sr_);
        lpZ_ = 0.0; shelfZ_ = 0.0;

        active_ = true;
    }

    bool active() const { return active_; }

    double process()
    {
        if(!active_) return 0.0;

        /* oscillator */
        const double f = hz_ * exp(pitchK_ * penv_);
        penv_ *= pcoef_;
        phase_ += f / sr_;
        if(phase_ >= 1.0) phase_ -= floor(phase_);
        const double s = sin(2.0 * M_PI * phase_);
        /* tanh(3.5 s) / tanh(3.5): a sine pushed towards a square */
        const double osc = s + harm_ * (tanh(3.5 * s) * 1.0018279 - s);

        /* amplitude */
        if(holdLeft_ > 0) --holdLeft_;
        else              env_ *= dcoef_;
        const double shaped = env_ > thr_ ? pow(env_, invRatio_)
                                          : env_ * tailGain_;
        if(atk_ < 1.0) { atk_ += atkStep_; if(atk_ > 1.0) atk_ = 1.0; }
        const double amp = shaped * atk_ * (1.0 + snapAmt_ * snap_);
        snap_ *= snapCoef_;

        double x = osc * amp;

        /* the trigger pulse */
        x += pulse_;
        pulse_ *= pulseCoef_;

        /* the noise burst, highpassed so it adds attack and not mud */
        if(noise_ > 1e-5)
        {
            rng_ = rng_ * 1664525u + 1013904223u;
            const double w = (double)(int32_t)rng_ * (1.0 / 2147483648.0);
            noiseLp_ += noiseLpC_ * (w - noiseLp_);
            x += (w - noiseLp_) * noise_;
            noise_ *= noiseCoef_;
        }

        /* EQ */
        if(lpOn_) { lpZ_ += lpC_ * (x - lpZ_); x = lpZ_; }
        if(shelfG_ > 0.0)
        {
            shelfZ_ += shelfC_ * (x - shelfZ_);
            x += shelfG_ * (x - shelfZ_);
        }

        if(declick_ != 0.0)
        {
            x += declick_;
            declick_ *= declickCoef_;
            if(fabs(declick_) < 1e-6) declick_ = 0.0;
        }

        if(env_ < 1e-4 && pulse_ < 1e-5 && declick_ == 0.0) active_ = false;

        last_ = x;

        /* Every knob at maximum stacks the overshoot, the pulse, the noise
         * and the treble lift to eight times the default peak. Above 1.6 —
         * which the default sound never reaches — the excess is rounded off
         * towards 2.6 instead of being handed to the bus as a hard clip. */
        const double ax = fabs(x);
        if(ax > 1.6) x = (x < 0.0 ? -1.0 : 1.0) * (1.6 + tanh(ax - 1.6));
        return x * kScale;
    }

private:
    static float clamp01(const float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    /* Level match to the circuit kick, so switching the model does not move
     * the kit balance: fitted so both kicks have the same loudest 20 ms at
     * their defaults and full velocity (the metric kVoiceTrim is fitted
     * with). The circuit kick is in volts, this voice is in +/-1. */
    static constexpr double kScale = 0.816;

    double sr_;
    bool   active_;
    double phase_, hz_;
    double pitchK_, penv_, pcoef_;
    double env_, dcoef_;
    int    holdLeft_;
    double atk_, atkStep_;
    double snap_, snapCoef_, snapAmt_;
    double harm_;
    double pulse_, pulseCoef_;
    double noise_, noiseCoef_, noiseLp_, noiseLpC_;
    double thr_, invRatio_, tailGain_;
    bool   lpOn_;
    double lpC_, lpZ_;
    double shelfG_, shelfC_, shelfZ_;
    double declick_, declickCoef_, last_;
    uint32_t rng_;
};

} /* namespace sc808 */

#endif /* SC808_BD_PUNCH_H */

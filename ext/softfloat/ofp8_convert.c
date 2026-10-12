/* OCP 8-bit float (OFP8 E4M3/E5M2) conversions for RISC-V Zvfofp8min v0.9,
   ported from the Spike fork's f32_to_f8.c/f8_to_f32.c with the format passed
   explicitly.  Canonical NaN is 0x7F for both formats. */

#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "internals.h"
#include "specialize.h"
#include "softfloat.h"

/* f32 -> OFP8, rounded once by softfloat_roundingMode.  Infinite results give
   the largest finite value when 'saturate', else E5M2 inf / E4M3 NaN 0x7F. */
uint_fast8_t f32_to_ofp8( float32_t a, bool e5m2, bool saturate )
{
    union ui32_f32 uA;
    uint_fast32_t uiA;
    bool sign;
    int_fast16_t exp, expOffset, expMax;
    uint_fast32_t frac;
    struct commonNaN commonNaN;
    uint_fast8_t uiZ, uiMax, uiInf, mantBits, shiftDist;
    uint_fast16_t hiddenBit, carryBit, sigOverflow, sig;
    uint_fast8_t roundingMode, roundIncrement, roundBits;
    bool roundNearEven, isTiny;

    if ( ! e5m2 ) {
        mantBits = 3;
        shiftDist = 16;
        hiddenBit = 0x80;
        expOffset = 0x79;
        expMax = 0x0E;
        sigOverflow = 0x0F;
    } else {
        mantBits = 2;
        shiftDist = 17;
        hiddenBit = 0x40;
        expOffset = 0x71;
        expMax = 0x1D;
        sigOverflow = 0x08;
    }
    carryBit = hiddenBit<<1;
    uA.f = a;
    uiA = uA.ui;
    sign = signF32UI( uiA );
    exp  = expF32UI( uiA );
    frac = fracF32UI( uiA );
    uiMax = ((uint_fast8_t) sign<<7) | (e5m2 ? 0x7B : 0x7E);
    uiInf = e5m2 ? (((uint_fast8_t) sign<<7) | 0x7C) : 0x7F;
    if ( exp == 0xFF ) {
        if ( frac ) {
            softfloat_f32UIToCommonNaN( uiA, &commonNaN );
            uiZ = 0x7F;
        } else {
            uiZ = saturate ? uiMax : uiInf;
        }
        return uiZ;
    }
    sig =
        frac>>shiftDist
            | ((frac & (((uint_fast32_t) 1<<shiftDist) - 1)) != 0);
    if ( ! (exp | sig) ) return (uint_fast8_t) sign<<7;
    sig |= hiddenBit;
    exp -= expOffset;
    roundingMode = softfloat_roundingMode;
    roundNearEven = (roundingMode == softfloat_round_near_even);
    roundIncrement = 0x8;
    if ( ! roundNearEven && (roundingMode != softfloat_round_near_maxMag) ) {
        roundIncrement =
            (roundingMode
                 == (sign ? softfloat_round_min : softfloat_round_max))
                ? 0xF
                : 0;
    }
    roundBits = sig & 0xF;
    if ( exp < 0 ) {
        isTiny =
            (softfloat_detectTininess == softfloat_tininess_beforeRounding)
                || (exp < -1) || (sig + roundIncrement < carryBit);
        sig = softfloat_shiftRightJam32( sig, -exp );
        exp = 0;
        roundBits = sig & 0xF;
        if ( isTiny && roundBits ) {
            softfloat_raiseFlags( softfloat_flag_underflow );
        }
    }
    sig = (sig + roundIncrement)>>4;
    sig &= ~(uint_fast16_t) (! (roundBits ^ 8) & roundNearEven);
#ifdef SOFTFLOAT_ROUND_ODD
    if ( roundBits && (roundingMode == softfloat_round_odd) ) sig |= 1;
#endif
    if ( (expMax < exp) || ((exp == expMax) && (sigOverflow <= sig)) ) {
        softfloat_raiseFlags(
            softfloat_flag_overflow | softfloat_flag_inexact );
        return (saturate || ! roundIncrement) ? uiMax : uiInf;
    }
    if ( roundBits ) softfloat_exceptionFlags |= softfloat_flag_inexact;
    if ( ! sig ) exp = 0;
    return
        ((uint_fast8_t) sign<<7) + ((uint_fast8_t) exp<<mantBits)
            + (uint_fast8_t) sig;
}

/* OFP8 -> bf16 bits: exact and raises no flag; every NaN, signaling E5M2
   included, becomes the canonical bf16 NaN 0x7FC0. */
uint_fast16_t ofp8_to_bf16UI( uint_fast8_t a, bool e5m2 )
{
    bool sign;
    int_fast16_t exp, expOffset;
    uint_fast8_t frac, hiddenBit, mantBits;

    sign = (a & 0x80) != 0;
    if ( ! e5m2 ) {
        if ( (a & 0x7F) == 0x7F ) return 0x7FC0;
        exp = (a>>3) & 0x0F;
        frac = a & 0x07;
        mantBits = 3;
        expOffset = 0x78;
    } else {
        exp = (a>>2) & 0x1F;
        frac = a & 0x03;
        if ( exp == 0x1F ) {
            return frac ? 0x7FC0 : (((uint_fast16_t) sign<<15) | 0x7F80);
        }
        mantBits = 2;
        expOffset = 0x70;
    }
    hiddenBit = 1<<mantBits;
    if ( ! exp ) {
        if ( ! frac ) return (uint_fast16_t) sign<<15;
        exp = 1;
        do {
            --exp;
            frac <<= 1;
        } while ( ! (frac & hiddenBit) );
        frac &= hiddenBit - 1;
    }
    return
        ((uint_fast16_t) sign<<15) | ((uint_fast16_t) (exp + expOffset)<<7)
            | ((uint_fast16_t) frac<<(7 - mantBits));
}

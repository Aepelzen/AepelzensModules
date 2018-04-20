#include <math.h>
#include "dsp/frame.hpp"

enum AeFilterType {
    AeLOWPASS,
    AeHIGHPASS
};

enum AeEQType {
    AeLOWSHELVE,
    AeHIGHSHELVE,
    AePEAKINGEQ
};

struct AeFilter {
    float x[2] = {0.0f};
    float y[2] =  {0.0f};

    float a0, a1, a2, b0, b1, b2;

    float process(float in) {
	float out = b0 * in + b1 * x[0] + b2 * x[1] - a1 * y[0] - a2 * y[1];

	//shift buffers
	x[1] = x[0];
	x[0] = in;
	y[1] = y[0];
	y[0] = out;

	return out;
    }

    void setCutoff(float f, float q, int type) {
	float w0 = 2*M_PI*f/engineGetSampleRate();
	float alpha = sin(w0)/(2.0f * q);

	float cs0 = cos(w0);

	switch(type) {
	case AeLOWPASS:
	    a0 =   1 + alpha;

	    b0 = (1 - cs0) /2 /a0;
	    b1 = (1 - cs0) /a0;
	    b2 = (1 - cs0) /2 /a0;
	    a1 = (-2 * cs0) /a0;
	    a2 = (1 - alpha)/a0;
	    break;
	case AeHIGHPASS:
	    a0 = 1 + alpha;
	    b0 = (1 + cs0)/2 /a0;
	    b1 = -(1 + cs0) /a0;
	    b2 = (1 + cs0) /2 /a0;
	    a1 = -2 * cs0 /a0;
	    a2 = (1 - alpha) /a0;
	}
    }
};

template<int CHANNELS>
struct AeFilterFrame : AeFilter {
    Frame<CHANNELS> x[2];
    Frame<CHANNELS> y[2];

    int channels = CHANNELS;

    AeFilterFrame() {
	init();
    }

    void init() {
	for(int i=0;i<channels;i++) {
	    x[0].samples[i] = 0.0f;
	    x[1].samples[i] = 0.0f;
	    y[0].samples[i] = 0.0f;
	    y[1].samples[i] = 0.0f;
	}
    }

    Frame<CHANNELS> process(Frame<CHANNELS> in) {
	Frame<CHANNELS> out;
	for(int i=0;i<channels;i++) {
	    out.samples[i] = b0 * in.samples[i] + b1 * x[0].samples[i] + b2 * x[1].samples[i] - a1 * y[0].samples[i] - a2 * y[1].samples[i];

	    //shift buffers
	    x[1].samples[i] = x[0].samples[i];
	    x[0].samples[i] = in.samples[i];
	    y[1].samples[i] = y[0].samples[i];
	    y[0].samples[i] = out.samples[i];
	}
	return out;
    }
};

struct AeFilterStereo : AeFilter {
    float xl[2] = {0.0f};
    float xr[2] = {0.0f};
    float yl[2] = {0.0f};
    float yr[2] = {0.0f};

    void process(float* inL, float* inR) {
	float l = b0 * *inL + b1 * xl[0] + b2 * xl[1] - a1 * yl[0] - a2 * yl[1];
	float r = b0 * *inR + b1 * xr[0] + b2 * xr[1] - a1 * yr[0] - a2 * yr[1];

	//shift buffers
	xl[1] = xl[0];
	xl[0] = *inL;
	xr[1] = xr[0];
	xr[0] = *inR;

	yl[1] = yl[0];
	yl[0] = l;
	yr[1] = yr[0];
	yr[0] = r;

	*inL = l;
	*inR = r;
    }
};

struct AeEqualizer {
    float x[2] = {0.0f};
    float y[2] = {0.0f};

    float a0, a1, a2, b0, b1, b2;

    float process(float in) {
	float out = b0 * in + b1 * x[0] + b2 * x[1] - a1 * y[0] - a2 * y[1];
	//shift buffers
	x[1] = x[0];
	x[0] = in;
	y[1] = y[0];
	y[0] = out;

	return out;
    }

    void setParams(float f, float q, float gaindb, AeEQType type) {

	float w0 = 2*M_PI*f/engineGetSampleRate();
	float alpha = sin(w0)/(2.0f * q);
	float cs0 = cos(w0);
	float A = pow(10, gaindb/40.0f);

	switch(type) {
	case AeLOWSHELVE:
	    a0 = (A + 1.0f) + (A - 1.0f) * cs0 + 2 * sqrt(A) * alpha;

	    b0 = A * ((A + 1.0f) - (A - 1.0f) * cs0 + 2 * sqrt(A) * alpha )/a0;
	    b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs0) /a0;
	    b2 = A * ((A + 1.0f) - (A - 1.0f) * cs0 - 2.0f * sqrt(A) * alpha ) /a0;
	    a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs0 ) /a0;
	    a2 = ((A + 1.0f) + (A - 1.0f) * cs0 - 2.0f * sqrt(A) * alpha) /a0;
	    break;
	case AeHIGHSHELVE:
	    a0 = (A + 1.0f) - (A - 1.0f) * cs0 + 2 * sqrt(A) * alpha;

	    b0 = A * ((A + 1.0f) + (A - 1.0f) * cs0 + 2 * sqrt(A) * alpha )/a0;
	    b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs0) /a0;
	    b2 = A * ((A + 1.0f) + (A - 1.0f) * cs0 - 2.0f * sqrt(A) * alpha ) /a0;
	    a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs0 ) /a0;
	    a2 = ((A + 1.0f) - (A - 1.0f) * cs0 - 2.0f * sqrt(A) * alpha) /a0;
	    break;
	case AePEAKINGEQ:
	    a0 = 1.0f + alpha/A;
	    b0 = (1.0f + alpha * A) /a0;
	    b1 = -2.0f * cs0 /a0;
	    b2 = (1.0f - alpha * A) /a0;
	    a1 = -2.0f * cs0 /a0;
	    a2 = (1.0f - alpha /A) /a0;
	}
    }
};

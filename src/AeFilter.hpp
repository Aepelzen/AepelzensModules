#include <math.h>
#include "dsp/frame.hpp"

enum AeFilterType {
    AeLOWPASS,
    AeHIGHPASS
};

struct AeFilter {
    float x[2];
    float y[2];

    float a0, a1, a2, b0, b1, b2;

    float process(float in) {
	float out = b0/a0 * in + b1/a0 * x[0] + b2/a0 * x[1] - a1/a0 * y[0] - a2/a0 * y[1];

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
	    b0 =  (1 - cs0)/2;
	    b1 =   1 - cs0;
	    b2 =  (1 - cs0)/2;
	    a0 =   1 + alpha;
	    a1 =  -2*cs0;
	    a2 =   1 - alpha;
	    break;
	case AeHIGHPASS:
	    b0 =  (1 + cs0)/2;
	    b1 = -(1 + cs0);
	    b2 =  (1 + cs0)/2;
	    a0 =   1 + alpha;
	    a1 =  -2*cs0;
	    a2 =   1 - alpha;
	}
    }
};

template<int CHANNELS>
struct AeFilterFrame : AeFilter {
    Frame<CHANNELS> x[2];
    Frame<CHANNELS> y[2];

    int channels = CHANNELS;

    Frame<CHANNELS> process(Frame<CHANNELS> in) {
	Frame<CHANNELS> out;
	for(int i=0;i<channels;i++) {
	    out.samples[i] = b0/a0 * in.samples[i] + b1/a0 * x[0].samples[i] + b2/a0 * x[1].samples[i] - a1/a0 * y[0].samples[i] - a2/a0 * y[1].samples[i];

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
    //first index for channel, second for shift
    float x[2][2];
    float y[2][2];

    void process(float* inL, float* inR) {
	float l = b0/a0 * *inL + b1/a0 * x[0][0] + b2/a0 * x[0][1] - a1/a0 * y[0][0] - a2/a0 * y[0][1];
	float r = b0/a0 * *inR + b1/a0 * x[1][0] + b2/a0 * x[1][1] - a1/a0 * y[1][0] - a2/a0 * y[1][1];

	//shift buffers
	x[0][1] = x[0][0];
	x[0][0] = *inL;
	x[1][1] = x[1][0];
	x[1][0] = *inR;

	y[0][1] = y[0][0];
	y[0][0] = l;
	y[1][1] = y[1][0];
	y[1][0] = r;

	*inL = l;
	*inR = r;
    }
};

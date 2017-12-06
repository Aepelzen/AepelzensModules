#include "aepelzen.hpp"
#include "dsp/digital.hpp"

const int NUM_STEPS = 16;
const int NUM_CHANNELS = 8;
const int NUM_GATES = NUM_STEPS * NUM_CHANNELS;

struct GateSEQ8 : Module {

	enum ParamIds {
		CLOCK_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		STEPS_PARAM,
		CHANNEL_STEPS_PARAM,
		CHANNEL2_STEPS_PARAM,
		CHANNEL3_STEPS_PARAM,
		CHANNEL4_STEPS_PARAM,
		CHANNEL5_STEPS_PARAM,
		CHANNEL6_STEPS_PARAM,
		CHANNEL7_STEPS_PARAM,
		CHANNEL8_STEPS_PARAM,
		CHANNEL_PROB_PARAM,
		CHANNEL2_PROB_PARAM,
		CHANNEL3_PROB_PARAM,
		CHANNEL4_PROB_PARAM,
		CHANNEL5_PROB_PARAM,
		CHANNEL6_PROB_PARAM,
		CHANNEL7_PROB_PARAM,
		CHANNEL8_PROB_PARAM,
		GATE1_PARAM,
		NUM_PARAMS = GATE1_PARAM + NUM_GATES + NUM_CHANNELS
	};
	enum InputIds {
		CLOCK_INPUT,
		EXT_CLOCK_INPUT,
		RESET_INPUT,
		STEPS_INPUT,
		CHANNEL_CLOCK_INPUT,
		NUM_INPUTS = CHANNEL_CLOCK_INPUT + NUM_CHANNELS
	};
	enum OutputIds {
		GATE1_OUTPUT,
		NUM_OUTPUTS = GATE1_OUTPUT + NUM_CHANNELS
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		GATE_LIGHTS,
		NUM_LIGHTS = GATE_LIGHTS + NUM_GATES
	};

	bool running = true;
	SchmittTrigger clockTrigger; // for external clock
	SchmittTrigger channelClockTrigger[NUM_CHANNELS]; // for external clock
	SchmittTrigger runningTrigger;
	SchmittTrigger resetTrigger;
	float phase = 0.0;
	int index = 0;
	int channel_index[NUM_CHANNELS] = {};
	SchmittTrigger gateTriggers[NUM_GATES];
	bool gateState[NUM_GATES] = {};
	float stepLights[NUM_GATES] = {};

	PulseGenerator gatePulse[NUM_CHANNELS];
	float prob = 0;

	GateSEQ8() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();

		// Gate values
		json_t *gatesJ = json_array();
		for (int i = 0; i < NUM_GATES; i++) {
			json_t *gateJ = json_integer((int) gateState[i]);
			json_array_append_new(gatesJ, gateJ);
		}
		json_object_set_new(rootJ, "gates", gatesJ);

		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// Gate values
		json_t *gatesJ = json_object_get(rootJ, "gates");
		for (int i = 0; i < NUM_GATES; i++) {
			json_t *gateJ = json_array_get(gatesJ, i);
			gateState[i] = !!json_integer_value(gateJ);
		}
	}

	void reset() override {
		for (int i = 0; i < NUM_GATES; i++) {
			gateState[i] = false;
		}
		for(int i=0; i< NUM_CHANNELS; i++) {
		  params[CHANNEL_STEPS_PARAM+i].value = NUM_STEPS;
		}
	}

	void randomize() override {
		for (int i = 0; i < NUM_GATES; i++) {
			gateState[i] = (randomf() > 0.5);
		}
	}
};


void GateSEQ8::step() {
	#ifdef v_050_dev
	float gSampleRate = engineGetSampleRate();
	#endif
	const float lightLambda = 0.075;
	// Run
	if (runningTrigger.process(params[RUN_PARAM].value)) {
		running = !running;
	}
	lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;

	bool nextStep = false;

	if (running) {
	  if (inputs[EXT_CLOCK_INPUT].active) {
	    // External clock
	    if (clockTrigger.process(inputs[EXT_CLOCK_INPUT].value)) {
	      //phase = 0.0;
	      nextStep = true;
	    }
	  }
	  else {
	    // Internal clock
	    float clockTime = powf(2.0, params[CLOCK_PARAM].value + inputs[CLOCK_INPUT].value);
	    phase += clockTime / gSampleRate;
	    if (phase >= 1.0) {
	      phase -= 1.0;
	      nextStep = true;
	    }
	  }
	}

	// Reset
	if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value)) {
		phase = 0.0;
		//index = 999;
		for (int y = 0; y < NUM_CHANNELS; y++) {
		  //channel_index[y] = 999;
		  channel_index[y] = -1;
		}
		nextStep = true;
		lights[RESET_LIGHT].value = 1.0;
	}

	bool pulse = false;
	bool channelStep = false;

	for (int y = 0; y < NUM_CHANNELS; y++) {
	  //channel clock overwrite
	  channelStep = false;
	  if(inputs[CHANNEL_CLOCK_INPUT + y].active) {
	    if (channelClockTrigger[y].process(inputs[CHANNEL_CLOCK_INPUT + y].value)) {
	      channelStep = true;
	    }
	  }
	  else {
	    channelStep = nextStep;
	  }
	  // Advance step
	  if (channelStep) {
	    //int numSteps = clampi(roundf(params[CHANNEL_STEPS_PARAM+y].value + inputs[STEPS_INPUT].value), 1, NUM_STEPS);
	    int numSteps = clampi(roundf(params[CHANNEL_STEPS_PARAM+y].value), 1, NUM_STEPS);
	    //printf("Channel: %i, numSteps: %i", y, numSteps);
	    channel_index[y] = (channel_index[y] + 1) % numSteps;
	    stepLights[y*NUM_STEPS + channel_index[y]] = 1.0;
	    gatePulse[y].trigger(1e-3);
	    prob = randomf();
	  }

	  pulse = gatePulse[y].process(1.0 / engineGetSampleRate());

	  bool gateOn = gateState[y*NUM_STEPS + channel_index[y]];
	  //probability
	  if(prob > params[CHANNEL_PROB_PARAM+y].value) {
	    gateOn = false;
	  }
	  gateOn = gateOn && !pulse;
	  float gate = (gateOn) ? 10.0 : 0.0;
	  outputs[GATE1_OUTPUT + y].value = gate;
	}

	lights[RESET_LIGHT].value -= lights[RESET_LIGHT].value / lightLambda / gSampleRate;

	// Gate buttons
	for (int i = 0; i < NUM_GATES; i++) {
		if (gateTriggers[i].process(params[GATE1_PARAM + i].value)) {
			gateState[i] = !gateState[i];
		}
		stepLights[i] -= stepLights[i] / lightLambda / gSampleRate;
		lights[GATE_LIGHTS + i].value = (gateState[i] >= 1.0) ? 0.7 - stepLights[i] : stepLights[i];
	}
}

template <typename BASE>
struct MuteLight : BASE {
	MuteLight() {
	  //this->box.size = Vec(20.0, 20.0);
	  this->box.size = mm2px(Vec(6.0, 6.0));
	}
};

GateSEQ8Widget::GateSEQ8Widget() {
	//printf("Parameter count %i:\n", GateSEQ8::NUM_PARAMS);
  //printf("Gate Param: %i, Step_Param: %i\n", GateSEQ8::GATE1_PARAM, GateSEQ8::CHANNEL_STEPS_PARAM);
	GateSEQ8 *module = new GateSEQ8();
	setModule(module);
	box.size = Vec(525, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/GateSEQ8.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

	addParam(createParam<RoundSmallBlackKnob>(Vec(17, 56), module, GateSEQ8::CLOCK_PARAM, -2.0, 10.0, 2.0));
	addParam(createParam<LEDButton>(Vec(60, 61-1), module, GateSEQ8::RUN_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(60+6, 61+5), module, GateSEQ8::RUNNING_LIGHT));
	addParam(createParam<LEDButton>(Vec(98, 61-1), module, GateSEQ8::RESET_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(98+6, 61+5), module, GateSEQ8::RESET_LIGHT));
	addParam(createParam<RoundSmallBlackSnapKnob>(Vec(132, 56), module, GateSEQ8::STEPS_PARAM, 1.0, NUM_STEPS, NUM_STEPS));

	static const float portX[8] = {19, 57, 96, 134, 173, 211, 250, 288};
	addInput(createInput<PJ301MPort>(Vec(portX[0]-1, 99-1), module, GateSEQ8::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX[1]-1, 99-1), module, GateSEQ8::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX[2]-1, 99-1), module, GateSEQ8::RESET_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX[3]-1, 99-1), module, GateSEQ8::STEPS_INPUT));

	for (int y = 0; y < NUM_CHANNELS; y++) {
		for (int x = 0; x < NUM_STEPS; x++) {
			int i = y*NUM_STEPS+x;
			// addParam(createParam<LEDButton>(Vec(32 + x*25, 155+y*25+3), module, GateSEQ8::GATE1_PARAM + i, 0.0, 1.0, 0.0));
			// addChild(createLight<SmallLight<GreenLight>>(Vec(38 + x*25, 156+y*25+8), module, GateSEQ8::GATE_LIGHTS + i));
			addParam(createParam<LEDBezel>(Vec(32 + x*25, 155+y*25), module, GateSEQ8::GATE1_PARAM + i, 0.0, 1.0, 0.0));
			addChild(createLight<MuteLight<GreenLight>>(Vec(32 + x*25 + 2, 155+y*25+2), module, GateSEQ8::GATE_LIGHTS + i));
		}
		addInput(createInput<PJ301MPort>(Vec(5, 155+y*25 - 1), module, GateSEQ8::CHANNEL_CLOCK_INPUT + y));
		addOutput(createOutput<PJ301MPort>(Vec(435, 155+y*25), module, GateSEQ8::GATE1_OUTPUT + y));
		addParam(createParam<Trimpot>(Vec(465, 155+y*25+3), module, GateSEQ8::CHANNEL_STEPS_PARAM + y, 1.0, NUM_STEPS, NUM_STEPS));
		addParam(createParam<Trimpot>(Vec(485, 155+y*25+3), module, GateSEQ8::CHANNEL_PROB_PARAM + y, 0.0, 1.0, 1.0));
	}
}

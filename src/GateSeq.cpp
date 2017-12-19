#include "aepelzen.hpp"
#include "dsp/digital.hpp"

const int NUM_STEPS = 16;
const int NUM_CHANNELS = 8;
const int NUM_GATES = NUM_STEPS * NUM_CHANNELS;

struct GateSeq : Module {

    enum ParamIds {
	LENGTH_PARAM,
	CLOCK_PARAM,
	RUN_PARAM,
	RESET_PARAM,
	INIT_PARAM,
	COPY_PARAM,
	MERGE_PARAM,
	PATTERN_SWITCH_MODE_PARAM,
	CHANNEL_PROB_PARAM,
	BANK_PARAM = CHANNEL_PROB_PARAM + NUM_CHANNELS,
	PATTERN_PARAM = BANK_PARAM + 8,
	GATE1_PARAM = PATTERN_PARAM + 8,
	NUM_PARAMS = GATE1_PARAM + NUM_GATES + NUM_CHANNELS
    };
    enum InputIds {
	CLOCK_INPUT,
	EXT_CLOCK_INPUT,
	RESET_INPUT,
	PATTERN_INPUT,
	CHANNEL_CLOCK_INPUT,
	CHANNEL_PROB_INPUT = CHANNEL_CLOCK_INPUT + NUM_CHANNELS,
	NUM_INPUTS = CHANNEL_PROB_INPUT + NUM_CHANNELS
    };
    enum OutputIds {
	GATE1_OUTPUT,
	NUM_OUTPUTS = GATE1_OUTPUT + NUM_CHANNELS
    };
    enum LightIds {
	RUNNING_LIGHT,
	RESET_LIGHT,
	COPY_LIGHT,
	LENGTH_LIGHT,
	MERGE_LIGHT,
	BANK_LIGHTS,
	PATTERN_LIGHTS = BANK_LIGHTS + 8,
	GATE_LIGHTS = PATTERN_LIGHTS + 8,
	NUM_LIGHTS = GATE_LIGHTS + NUM_GATES + NUM_GATES
    };

    GateSeq() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
    void step() override;
    json_t *toJson() override;
    void fromJson(json_t *rootJ) override;
    void initializePattern(int bank, int pattern);
    void copyPattern(int basePattern, int bank, int pattern);
    void processPatternSelection();

    struct patternInfo {
	bool gates[NUM_GATES] = {false};
	int length[NUM_CHANNELS] = { 16, 16, 16, 16, 16, 16, 16, 16};
	//float prob[NUM_CHANNELS] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    };

    patternInfo patterns [64] = {};
    patternInfo* currentPattern;

    int bank = 0;
    int pattern = 0;
    //source pattern for copying and merging (this uses the actual pattern index 0..64)
    int basePattern = 0;

    SchmittTrigger clockTrigger; // for external clock
    SchmittTrigger channelClockTrigger[NUM_CHANNELS]; // for external clock
    SchmittTrigger runningTrigger;
    SchmittTrigger resetTrigger;
    SchmittTrigger initTrigger;
    SchmittTrigger copyTrigger;
    SchmittTrigger lengthTrigger;
    SchmittTrigger gateTriggers[NUM_GATES];
    SchmittTrigger bankTriggers[8];
    SchmittTrigger patternTriggers[8];

    PulseGenerator gatePulse[NUM_CHANNELS];

    float stepLights[NUM_GATES] = {};
    int channel_index[NUM_CHANNELS] = {};
    bool running = true;
    bool copyMode = false;
    bool lengthMode = false;
    float phase = 0.0;
    float prob = 0;

    void reset() override {
	for(int y=0;y<64;y++) {
	    for (int i = 0; i < NUM_GATES; i++) {
		patterns[y].gates[i] = false;
	    }
	    for (int i=0; i<NUM_CHANNELS; i++) {
		patterns[y].length[i] = 16;
	    }
	}
	bank = 0;
	pattern = 0;
    }

    void randomize() override {
	for (int i=0; i<NUM_CHANNELS; i++) {
	    for (int y=0; y<NUM_STEPS; y++) {
		currentPattern->gates[i*NUM_CHANNELS + y] = (randomf() > 0.5);
	    }
	    currentPattern->length[i] = (int)(randomf()*15) + 1;
	}
    }
};


void GateSeq::step() {
    float gSampleRate = engineGetSampleRate();
    //const float lightLambda = 0.075;
    const float lightLambda = 0.1;

    // Run
    if (runningTrigger.process(params[RUN_PARAM].value))
	running = !running;
    lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;

    processPatternSelection();
    bool nextStep = false;

    if(lengthTrigger.process(params[LENGTH_PARAM].value)) {
	lengthMode = !lengthMode;
    }
    lights[LENGTH_LIGHT].value = (lengthMode) ? 1.0 : 0.0;

    if (running) {
	if (inputs[EXT_CLOCK_INPUT].active) {
	    // External clock
	    if (clockTrigger.process(inputs[EXT_CLOCK_INPUT].value)) {
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

	bool pulse = false;
	bool channelStep = false;

	for (int y = 0; y < NUM_CHANNELS; y++) {
	    float channelProb = clampf(inputs[CHANNEL_PROB_INPUT + y].value /5.0 + params[CHANNEL_PROB_PARAM + y].value, 0.0, 1.0);
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
		//int numSteps = clampi(roundf(params[CHANNEL_STEPS_PARAM+y].value), 1, NUM_STEPS);
		int numSteps = currentPattern->length[y];
		channel_index[y] = (channel_index[y] + 1) % numSteps;
		stepLights[y*NUM_STEPS + channel_index[y]] = 1.0;
		gatePulse[y].trigger(1e-3);
		//only compute new random number for active steps
		if (currentPattern->gates[y*NUM_STEPS + channel_index[y]] && channelProb < 1) {
		    prob = randomf();
		}
	    }

	    pulse = gatePulse[y].process(1.0 / engineGetSampleRate());
	    bool gateOn = currentPattern->gates[y*NUM_STEPS + channel_index[y]];
	    //probability
	    if(prob > channelProb) {
		gateOn = false;
	    }
	    gateOn = gateOn && !pulse;
	    outputs[GATE1_OUTPUT + y].value = (gateOn) ? 10.0 : 0.0;
	}
    }
    else {
	//clear outputs, otherwise it holds it's last value
	for(int y=0;y<NUM_CHANNELS;y++) {
	    outputs[GATE1_OUTPUT + y].value = 0;
	}
    }

    // Reset
    if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value)) {
	phase = 0.0;
	for (int y = 0; y < NUM_CHANNELS; y++) {
	    channel_index[y] = 0;
	}
	nextStep = true;
	lights[RESET_LIGHT].value = 1.0;
    }
    lights[RESET_LIGHT].value -= lights[RESET_LIGHT].value / lightLambda / gSampleRate;

    // Gate buttons
    for (int i = 0; i < NUM_GATES; i++) {
	if (gateTriggers[i].process(params[GATE1_PARAM + i].value)) {
	    if(lengthMode) {
		currentPattern->length[i/NUM_STEPS] = (i % NUM_STEPS ) + 1;
	    }
	    else
		currentPattern->gates[i] = !currentPattern->gates[i];
	}
	stepLights[i] -= stepLights[i] / lightLambda / gSampleRate;
	lights[GATE_LIGHTS + 2*i].value = (currentPattern->gates[i] >= 1.0) ? 0.7 - stepLights[i] : stepLights[i];
	lights[GATE_LIGHTS + 2*i + 1].value = ( lengthMode && (i % NUM_STEPS + 1) == currentPattern->length[i/NUM_STEPS]) ? 1.0 : 0.0;
    }
}

template <typename BASE>
struct MuteLight : BASE {
    MuteLight() {
	this->box.size = mm2px(Vec(6.0, 6.0));
    }
};

GateSeqWidget::GateSeqWidget() {
    GateSeq *module = new GateSeq();
    setModule(module);
    box.size = Vec(525, 380);

    {
	SVGPanel *panel = new SVGPanel();
	panel->box.size = box.size;
	panel->setBackground(SVG::load(assetPlugin(plugin, "res/GateSeq.svg")));
	addChild(panel);
    }

    addChild(createScrew<ScrewSilver>(Vec(15, 0)));
    addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
    addChild(createScrew<ScrewSilver>(Vec(15, 365)));
    addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

    addParam(createParam<RoundSmallBlackKnob>(Vec(17, 56), module, GateSeq::CLOCK_PARAM, -2.0, 10.0, 2.0));
    addParam(createParam<LEDButton>(Vec(60, 61-1), module, GateSeq::RUN_PARAM, 0.0, 1.0, 0.0));
    addChild(createLight<SmallLight<GreenLight>>(Vec(60+6, 61+5), module, GateSeq::RUNNING_LIGHT));
    addParam(createParam<LEDButton>(Vec(98, 61-1), module, GateSeq::RESET_PARAM, 0.0, 1.0, 0.0));
    addChild(createLight<SmallLight<GreenLight>>(Vec(98+6, 61+5), module, GateSeq::RESET_LIGHT));

    static const float portX[8] = {19, 57, 96, 134, 173, 211, 250, 288};
    addInput(createInput<PJ301MPort>(Vec(portX[0]-1, 99-1), module, GateSeq::CLOCK_INPUT));
    addInput(createInput<PJ301MPort>(Vec(portX[1]-1, 99-1), module, GateSeq::EXT_CLOCK_INPUT));
    addInput(createInput<PJ301MPort>(Vec(portX[2]-1, 99-1), module, GateSeq::RESET_INPUT));
    addInput(createInput<PJ301MPort>(Vec(portX[3]-1, 99-1), module, GateSeq::PATTERN_INPUT));

    addParam(createParam<LEDBezel>(Vec(170, 55), module, GateSeq::COPY_PARAM , 0.0, 1.0, 0.0));
    addChild(createLight<MuteLight<YellowLight>>(Vec(172, 57), module, GateSeq::COPY_LIGHT));
    addParam(createParam<LEDBezel>(Vec(170, 98), module, GateSeq::INIT_PARAM , 0.0, 1.0, 0.0));

    addParam(createParam<LEDBezel>(Vec(200, 98), module, GateSeq::LENGTH_PARAM , 0.0, 1.0, 0.0));
    addChild(createLight<MuteLight<GreenRedLight>>(Vec(202, 100), module, GateSeq::LENGTH_LIGHT));
    addParam(createParam<CKSS>(Vec(139, 55), module, GateSeq::PATTERN_SWITCH_MODE_PARAM , 0.0, 1.0, 0.0));

    addParam(createParam<LEDBezel>(Vec(465, 50), module, GateSeq::MERGE_PARAM , 0.0, 1.0, 0.0));
    addChild(createLight<MuteLight<RedLight>>(Vec(467, 52), module, GateSeq::MERGE_LIGHT));

    //pattern/bank buttons
    for(int i=0;i<8;i++) {
	addParam(createParam<LEDBezel>(Vec(252 + i*24, 55), module, GateSeq::BANK_PARAM + i, 0.0, 1.0, 0.0));
	addChild(createLight<MuteLight<GreenLight>>(Vec(254 + i*24, 57), module, GateSeq::BANK_LIGHTS + i));
	addParam(createParam<LEDBezel>(Vec(252 + i*24, 98), module, GateSeq::PATTERN_PARAM + i, 0.0, 1.0, 0.0));
	addChild(createLight<MuteLight<GreenLight>>(Vec(254 + i*24, 100), module, GateSeq::PATTERN_LIGHTS + i));
    }

    for (int y = 0; y < NUM_CHANNELS; y++) {
	for (int x = 0; x < NUM_STEPS; x++) {
	    int i = y*NUM_STEPS+x;
	    addParam(createParam<LEDBezel>(Vec(62 + x*25, 155+y*25), module, GateSeq::GATE1_PARAM + i, 0.0, 1.0, 0.0));
	    addChild(createLight<MuteLight<GreenRedLight>>(Vec(62 + x*25 + 2, 155+y*25+2), module, GateSeq::GATE_LIGHTS + 2*i));
	}
	addInput(createInput<PJ301MPort>(Vec(5, 155+y*25 - 1.5), module, GateSeq::CHANNEL_CLOCK_INPUT + y));
	addInput(createInput<PJ301MPort>(Vec(32, 155+y*25 - 1.5), module, GateSeq::CHANNEL_PROB_INPUT + y));
	addOutput(createOutput<PJ301MPort>(Vec(465, 155+y*25 - 1.5), module, GateSeq::GATE1_OUTPUT + y));
	addParam(createParam<Trimpot>(Vec(495, 155+y*25 + 1.5), module, GateSeq::CHANNEL_PROB_PARAM + y, 0.0, 1.0, 1.0));
    }
}

void GateSeq::processPatternSelection() {
    if(initTrigger.process(params[INIT_PARAM].value))
	initializePattern(bank, pattern);

    //copy pattern
    if(copyTrigger.process(params[COPY_PARAM].value)) {
	if(copyMode)
	    copyPattern(basePattern, bank, pattern);
	else
	    basePattern = 8*bank + pattern;
	copyMode = (!copyMode);
    }
    lights[COPY_LIGHT].value = (copyMode) ? 1.0 : 0.0;

    //bank
    for(int i=0;i<8;i++) {
	if(bankTriggers[i].process(params[BANK_PARAM + i].value)) {
	    bank = i;
	    //Switch to first pattern in bank (TODO: do i really want this?)
	    pattern = 0;
	    break;
	}
	lights[BANK_LIGHTS + i].value = (bank == i) ? 1.0 : 0.0;
    }
    //pattern
    for(int i=0;i<8;i++) {
	if(inputs[PATTERN_INPUT].active) {
	    int in = clampi(trunc(inputs[PATTERN_INPUT].value),0 , 7);
	    if (in != pattern && params[PATTERN_SWITCH_MODE_PARAM].value) {
		for(int y=0;y<NUM_CHANNELS;y++) {
		    channel_index[y] = -1;
		}
	    }
	    pattern = in;
	}
	else if(patternTriggers[i].process(params[PATTERN_PARAM + i].value)) {
	    pattern = i;
	    //reset index
	    if(params[PATTERN_SWITCH_MODE_PARAM].value) {
		for(int y=0;y<NUM_CHANNELS;y++) {
		    channel_index[y] = -1;
		}
	    }
	    break;
	}
    }
    for(int i=0;i<8;i++) {
	lights[PATTERN_LIGHTS + i].value = (pattern == i) ? 1.0 : 0.0;
    }
    currentPattern = &patterns[8*bank + pattern];
}

void GateSeq::initializePattern(int bank, int pattern) {
    for(int i=0;i<NUM_GATES;i++) {
	currentPattern->gates[i] = 0;
    }

    for (int i = 0; i<NUM_CHANNELS; i++) {
	currentPattern->length[i] = 16;
	//currentPatternp->rob[i] = 1;
    }
}

/**
   Copy a pattern

   @param basePattern The Source pattern to be copied
   @param bank The bank of the destination pattern
   @param pattern The pattern of the destination pattern
*/
void GateSeq::copyPattern(int basePattern, int bank, int pattern) {
    //currentPattern = &patterns[8*bank + pattern];
    printf("Copying pattern: %d to bank: %d, pattern:%d\n", basePattern, bank, pattern);
    for (int i=0; i<NUM_GATES; i++) {
	patterns[8*bank + pattern].gates[i] = patterns[basePattern].gates[i];
    }
    for(int i=0;i<NUM_CHANNELS;i++) {
	patterns[8*bank + pattern].length[i] = patterns[basePattern].length[i];
    }
}

json_t* GateSeq::toJson() {
    json_t *rootJ = json_object();

    //patterns
    json_t *patternsJ = json_array();
    json_t *lengthsJ = json_array();

    for(int y=0;y<64;y++) {
	// Gate values
	json_t *gatesJ = json_array();
	for (int i = 0; i < NUM_GATES; i++) {
	    json_t *gateJ = json_integer((int) patterns[y].gates[i]);
	    json_array_append_new(gatesJ, gateJ);
	}
	json_array_append_new(patternsJ, gatesJ);

	//second array for lengths to keep things simple
	json_t *pLengthJ = json_array();
	for(int i=0;i<NUM_CHANNELS;i++) {
	    json_t* lengthJ = json_integer(patterns[y].length[i]);
	    json_array_append_new(pLengthJ, lengthJ);
	}
	json_array_append_new(lengthsJ, pLengthJ);
    }
    json_object_set_new(rootJ, "patterns", patternsJ);
    json_object_set_new(rootJ, "lengths", lengthsJ);

    json_t *activePatternJ = json_integer(pattern);
    json_object_set_new(rootJ, "pattern", activePatternJ);
    json_t *activeBankJ = json_integer(bank);
    json_object_set_new(rootJ, "bank", activeBankJ);
    return rootJ;
}

void GateSeq::fromJson(json_t *rootJ) {
    json_t *patternsJ = json_object_get(rootJ, "patterns");
    json_t *lengthsJ = json_object_get(rootJ, "lengths");

    for(int y=0;y<64;y++) {
	// Gate values
	json_t *gatesJ = json_array_get(patternsJ, y);
	for (int i = 0; i < NUM_GATES; i++) {
	    json_t *gateJ = json_array_get(gatesJ, i);
	    //patterns[y][i] = json_integer_value(gateJ);
	    patterns[y].gates[i] = json_integer_value(gateJ);
	}
	json_t *pLengthsJ = json_array_get(lengthsJ, y);
	for(int i=0;i<NUM_CHANNELS;i++) {
	    json_t *lengthJ = json_array_get(pLengthsJ, i);
	    patterns[y].length[i] = json_integer_value(lengthJ);
	}
    }
    json_t * patternJ = json_object_get(rootJ, "pattern");
    pattern = json_integer_value(patternJ);
    json_t * bankJ = json_object_get(rootJ, "bank");
    bank = json_integer_value(bankJ);

    currentPattern = &patterns[8*bank + pattern];
}

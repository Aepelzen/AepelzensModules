#include "aepelzen.hpp"
#include "dsp/digital.hpp"

const int NUM_STEPS = 16;
const int NUM_CHANNELS = 8;
const int NUM_GATES = NUM_STEPS * NUM_CHANNELS;

struct GateSeq : Module {

    enum ParamIds {
	TEST_PARAM,
	CLOCK_PARAM,
	RUN_PARAM,
	RESET_PARAM,
	INIT_PARAM,
	COPY_PARAM,
	MERGE_PARAM,
	CHANNEL_STEPS_PARAM,
	CHANNEL_PROB_PARAM = CHANNEL_STEPS_PARAM + NUM_CHANNELS,
	BANK_PARAM = CHANNEL_PROB_PARAM + NUM_CHANNELS,
	PATTERN_PARAM = BANK_PARAM + 8,
	GATE1_PARAM = PATTERN_PARAM + 8,
	NUM_PARAMS = GATE1_PARAM + NUM_GATES + NUM_CHANNELS
    };
    enum InputIds {
	CLOCK_INPUT,
	EXT_CLOCK_INPUT,
	RESET_INPUT,
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
	MERGE_LIGHT,
	BANK_LIGHTS,
	PATTERN_LIGHTS = BANK_LIGHTS + 8,
	GATE_LIGHTS = PATTERN_LIGHTS + 8,
	NUM_LIGHTS = GATE_LIGHTS + NUM_GATES
    };

    GateSeq() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
    void step() override;
    json_t *toJson() override;
    void fromJson(json_t *rootJ) override;

    //Stuff for copying patterns
    float lengthValues[8] = {};
    float probValues[8] = {};

    int bank = 0;
    int pattern = 0;

    void initializePattern(int bank, int pattern);
    void copyPattern(int basePattern, int bank, int pattern);
    void processPatternSelection();

    SchmittTrigger clockTrigger; // for external clock
    SchmittTrigger channelClockTrigger[NUM_CHANNELS]; // for external clock
    SchmittTrigger runningTrigger;
    SchmittTrigger resetTrigger;
    SchmittTrigger initTrigger;
    SchmittTrigger copyTrigger;
    SchmittTrigger gateTriggers[NUM_GATES];
    PulseGenerator gatePulse[NUM_CHANNELS];
    float stepLights[NUM_GATES] = {};

    bool running = true;
    bool copyMode = false;
    float phase = 0.0;
    int channel_index[NUM_CHANNELS] = {};
    float prob = 0;

    SchmittTrigger bankTriggers[8];    
    //source pattern for copying and merging (this uses the actual pattern index 0..64)
    int basePattern = 0;
    SchmittTrigger patternTriggers[8];
    bool patterns [64][NUM_GATES] = {};
    //this used to be the array of gate values. Now it points to the first gate value of the
    //curent pattern in patterns so i can just keep using it as before (TODO: maybe get rid of this)
    bool* gateState = &patterns[0][0];
    //bool gateState[NUM_GATES] = {};
    
    void reset() override {
	for(int y=0;y<64;y++) {
	    for (int i = 0; i < NUM_GATES; i++) {
		patterns[y][i] = false;
	    }
	}
	bank = 0;
	pattern = 0;
    }

    void randomize() override {
	for (int i = 0; i < NUM_GATES; i++) {
	    gateState[i] = (randomf() > 0.5);
	}
    }
};


void GateSeq::step() {
    float gSampleRate = engineGetSampleRate();
    const float lightLambda = 0.075;

    // Run
    if (runningTrigger.process(params[RUN_PARAM].value))
	running = !running;
    lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;

    processPatternSelection();
    bool nextStep = false;

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
		int numSteps = clampi(roundf(params[CHANNEL_STEPS_PARAM+y].value), 1, NUM_STEPS);
		channel_index[y] = (channel_index[y] + 1) % numSteps;
		stepLights[y*NUM_STEPS + channel_index[y]] = 1.0;
		gatePulse[y].trigger(1e-3);
		//only compute new random number for active steps
		if (gateState[y*NUM_STEPS + channel_index[y]] && channelProb < 1) {
		    prob = randomf();
		}
	    }

	    pulse = gatePulse[y].process(1.0 / engineGetSampleRate());
	    bool gateOn = gateState[y*NUM_STEPS + channel_index[y]];
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

struct InitButton : LEDBezel {
    void onMouseDown(EventMouseDown &e) override {
	GateSeqWidget *parent = dynamic_cast<GateSeqWidget*>(this->parent);
	GateSeq *module = dynamic_cast<GateSeq*>(this->module);
	if (module && parent) {
	    module->initializePattern(module->bank, module->pattern);
	    parent->updateValues();   
	}
	LEDBezel::onMouseDown(e);
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

    addParam(createParam<InitButton>(Vec(200, 30), module, GateSeq::INIT_PARAM , 0.0, 1.0, 0.0));
    addParam(createParam<LEDBezel>(Vec(200, 70), module, GateSeq::COPY_PARAM , 0.0, 1.0, 0.0));
    addChild(createLight<MuteLight<RedLight>>(Vec(202, 72), module, GateSeq::COPY_LIGHT));

    addParam(createParam<LEDBezel>(Vec(465, 30), module, GateSeq::MERGE_PARAM , 0.0, 1.0, 0.0));
    addChild(createLight<MuteLight<RedLight>>(Vec(467, 32), module, GateSeq::MERGE_LIGHT));

    //pattern/bank buttons
    for(int i=0;i<8;i++) {
	addParam(createParam<LEDBezel>(Vec(252 + i*24, 30), module, GateSeq::BANK_PARAM + i, 0.0, 1.0, 0.0));
	addChild(createLight<MuteLight<GreenLight>>(Vec(254 + i*24, 32), module, GateSeq::BANK_LIGHTS + i));
	addParam(createParam<LEDBezel>(Vec(252 + i*24, 70), module, GateSeq::PATTERN_PARAM + i, 0.0, 1.0, 0.0));
	addChild(createLight<MuteLight<GreenLight>>(Vec(254 + i*24, 72), module, GateSeq::PATTERN_LIGHTS + i));
    }

    for (int y = 0; y < NUM_CHANNELS; y++) {
	for (int x = 0; x < NUM_STEPS; x++) {
	    int i = y*NUM_STEPS+x;
	    addParam(createParam<LEDBezel>(Vec(60 + x*24, 155+y*25), module, GateSeq::GATE1_PARAM + i, 0.0, 1.0, 0.0));
	    addChild(createLight<MuteLight<GreenLight>>(Vec(60 + x*24 + 2, 155+y*25+2), module, GateSeq::GATE_LIGHTS + i));
	}
	addInput(createInput<PJ301MPort>(Vec(5, 155+y*25 - 1.5), module, GateSeq::CHANNEL_CLOCK_INPUT + y));
	addInput(createInput<PJ301MPort>(Vec(30, 155+y*25 - 1.5), module, GateSeq::CHANNEL_PROB_INPUT + y));
	addOutput(createOutput<PJ301MPort>(Vec(445, 155+y*25 - 1.5), module, GateSeq::GATE1_OUTPUT + y));
	//length and prob
	lengthParams[y] = createParam<Trimpot>(Vec(475, 155+y*25 + 1.5), module, GateSeq::CHANNEL_STEPS_PARAM + y, 1.0, NUM_STEPS, NUM_STEPS);
	probParams[y] = createParam<Trimpot>(Vec(495, 155+y*25 + 1.5), module, GateSeq::CHANNEL_PROB_PARAM + y, 0.0, 1.0, 1.0);
	addParam(lengthParams[y]);
	addParam(probParams[y]);
    }
    //updateValues();
}

void GateSeq::processPatternSelection() {
    if(initTrigger.process(params[INIT_PARAM].value))
    	initializePattern(bank, pattern);

    //copy pattern
    if(copyTrigger.process(params[COPY_PARAM].value))
	copyMode = (!copyMode);
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
	if(patternTriggers[i].process(params[PATTERN_PARAM + i].value)) {
	    if(copyMode) {
		basePattern = pattern;
		copyPattern(basePattern, bank, i);
		copyMode = false;
	    }
	    pattern = i;
	    break;
	}
	lights[PATTERN_LIGHTS + i].value = (pattern == i) ? 1.0 : 0.0;
    }
    gateState = &patterns[pattern + bank*8][0];
}

void GateSeq::initializePattern(int bank, int pattern) {
    gateState = &patterns[8*bank + pattern][0];
    for(int i=0;i<NUM_GATES;i++) {
    	gateState[i] = 0;
    }

    //TODO: find a way to set parameters update knob positions
    for (int i = 0; i<NUM_CHANNELS; i++) {
    	lengthValues[i] = 16;
	probValues[i] = 1;
    }
}

void GateSeqWidget::updateValues() {
    GateSeq *module = dynamic_cast<GateSeq*>(this->module);
    for(int i=0;i<NUM_CHANNELS;i++) {
	lengthParams[i]->setValue(module->lengthValues[i]);
	probParams[i]->setValue(module->probValues[i]);
    }    
}
/**
   Copy a pattern

   @param basePattern The Source pattern to be copied
   @param bank The bank of the destination pattern
   @param pattern The pattern of the destination pattern
*/
void GateSeq::copyPattern(int basePattern, int bank, int pattern) {
    gateState = &patterns[8*bank + pattern][0];
    for (int i=0; i<NUM_GATES; i++) {
	gateState[i] = patterns[basePattern][i];
    }
}

json_t* GateSeq::toJson() {
    json_t *rootJ = json_object();

    //patterns
    json_t *patternsJ = json_array();
    for(int y=0;y<64;y++) {
	// Gate values
	json_t *gatesJ = json_array();
	for (int i = 0; i < NUM_GATES; i++) {
	    //json_t *gateJ = json_integer((int) gateState[i]);
	    json_t *gateJ = json_integer((int) patterns[y][i]);
	    json_array_append_new(gatesJ, gateJ);
	}
	json_array_append_new(patternsJ, gatesJ);
    }
    json_object_set_new(rootJ, "patterns", patternsJ);

    json_t *activePatternJ = json_integer(pattern);
    json_object_set_new(rootJ, "pattern", activePatternJ);
    json_t *activeBankJ = json_integer(bank);
    json_object_set_new(rootJ, "bank", activeBankJ);
    return rootJ;
}

void GateSeq::fromJson(json_t *rootJ) {
    json_t *patternsJ = json_object_get(rootJ, "patterns");

    for(int y=0;y<64;y++) {
	// Gate values
	json_t *gatesJ = json_array_get(patternsJ, y);
	for (int i = 0; i < NUM_GATES; i++) {
	    json_t *gateJ = json_array_get(gatesJ, i);
	    patterns[y][i] = json_integer_value(gateJ);
	}
    }
    json_t * patternJ = json_object_get(rootJ, "pattern");
    pattern = json_integer_value(patternJ);
    json_t * bankJ = json_object_get(rootJ, "bank");
    bank = json_integer_value(bankJ);

    //update gateStates
    gateState = &patterns[pattern + bank*8][0];
}

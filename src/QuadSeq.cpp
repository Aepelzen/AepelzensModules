#include "aepelzen.hpp"
#include "dsp/digital.hpp"

#define NUM_CHANNELS 4

struct QuadSeq : Module {
  enum ParamIds {
    CLOCK_PARAM,
    RUN_PARAM,
    RESET_PARAM,
    CHANNEL_STEPS_PARAM,
    CHANNEL_RANGE_PARAM = CHANNEL_STEPS_PARAM + NUM_CHANNELS,
    CHANNEL_MODE_PARAM = CHANNEL_RANGE_PARAM + NUM_CHANNELS,
    ROW1_PARAM = CHANNEL_MODE_PARAM + NUM_CHANNELS,
    ROW2_PARAM = ROW1_PARAM + 8,
    ROW3_PARAM = ROW2_PARAM + 8,
    ROW4_PARAM = ROW3_PARAM + 8,
    CHANNEL_PROB_PARAM = ROW4_PARAM + 8,
    STEP_SELECT_PARAM = CHANNEL_PROB_PARAM + NUM_CHANNELS,
    NUM_PARAMS = STEP_SELECT_PARAM + 8
  };
  enum InputIds {
    CLOCK_INPUT,
    EXT_CLOCK_INPUT,
    RESET_INPUT,
    CHANNEL_CLOCK_INPUT,
    NUM_INPUTS = CHANNEL_CLOCK_INPUT + NUM_CHANNELS
  };
  enum OutputIds {
    ROW1_OUTPUT,
    NUM_OUTPUTS = ROW1_OUTPUT + NUM_CHANNELS
  };
  enum LightIds {
    RUNNING_LIGHT,
    RESET_LIGHT,
    CHANNEL_LIGHTS,
    NUM_LIGHTS = CHANNEL_LIGHTS + 8 * NUM_CHANNELS
  };

  enum PlaybackModes {
    MODE_FORWARD,
    MODE_BACKWARD,
    MODE_ALTERNATING,
    MODE_RANDOM_NEIGHBOUR,
    MODE_RANDOM
  };

  bool running = true;
  bool manualStepSelect = false;
  SchmittTrigger clockTrigger; // for external clock
  SchmittTrigger channelClockTrigger[NUM_CHANNELS]; // for external clock
  // For buttons
  SchmittTrigger runningTrigger;
  SchmittTrigger resetTrigger;

  SchmittTrigger stepSelectTrigger[8];

  float phase = 0.0;
  int index = 0;

  float resetLight = 0.0;
  float stepLights[NUM_CHANNELS][8] = {};

  int channel_index[NUM_CHANNELS] = {};
  float rowValue[NUM_CHANNELS] = {};
  //playback direction: true for forward, false for backward
  bool direction[NUM_CHANNELS] = {};

  QuadSeq() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
    reset();
  }
  void step() override;

  json_t *toJson() override {
    json_t *rootJ = json_object();

    // running
    json_object_set_new(rootJ, "running", json_boolean(running));
    return rootJ;
  }

  void fromJson(json_t *rootJ) override {
    // running
    json_t *runningJ = json_object_get(rootJ, "running");
    if (runningJ)
      running = json_is_true(runningJ);
  }

  void reset() override {
    for(int i=0;i<NUM_CHANNELS;i++) {
      direction[i] = true;
      channel_index[i] = 0;
    }
  }
};


void QuadSeq::step() {
  const float lightLambda = 0.075;
  // Run
  if (runningTrigger.process(params[RUN_PARAM].value)) {
    running = !running;
    manualStepSelect = false;
  }
  lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;

  bool nextStep = false;

  if (running) {
    if (inputs[EXT_CLOCK_INPUT].active) {
      // External clock
      if (clockTrigger.process(inputs[EXT_CLOCK_INPUT].value)) {
	phase = 0.0;
	nextStep = true;
      }
    }
    else {
      // Internal clock
      float clockTime = powf(2.0, params[CLOCK_PARAM].value + inputs[CLOCK_INPUT].value);
      phase += clockTime / engineGetSampleRate();
      if (phase >= 1.0) {
	phase -= 1.0;
	nextStep = true;
      }
    }
  }

  // Reset
  if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value)) {
    phase = 0.0;
    //index = 8;
    nextStep = true;
    resetLight = 1.0;
    for (int i=0;i<NUM_CHANNELS;i++) {
      channel_index[i] = -1;
    }
  }

  //manual step selection
  for(int i=0;i<8;i++) {
      if(stepSelectTrigger[i].process(params[STEP_SELECT_PARAM + i].value)) {
	  for(int y=0;y<NUM_CHANNELS;y++) {
	      channel_index[y] = i;
	  }
	  manualStepSelect = true;
	  break;
      }
  }

  bool channelStep = false;

  for (int y = 0; y < NUM_CHANNELS; y++) {
    //channel clock overwrite
    channelStep = false;
    if(inputs[CHANNEL_CLOCK_INPUT + y].active) {
      if (channelClockTrigger[y].process(inputs[CHANNEL_CLOCK_INPUT + y].value) && running) {
	channelStep = true;
      }
    }
    else {
      channelStep = nextStep;
    }

    // Advance step
    if (channelStep) {
      int numSteps = (int) params[CHANNEL_STEPS_PARAM + y].value;
      int mode = (int)params[CHANNEL_MODE_PARAM + y].value;
      float prob = params[CHANNEL_PROB_PARAM + y].value * 2.0f;
      int stepsize = 1;

      if (mode == MODE_RANDOM_NEIGHBOUR) {
	  mode = (randomUniform() > 0.5) ? MODE_FORWARD : MODE_BACKWARD;
      }

      //values smaller than zero repeat a step, greater than zero skip
      if(prob < 1.0f) {
	  stepsize = (randomUniform() > prob) ? 0 : 1;
      }
      else if (prob > 1.0f) {
	  stepsize = (randomUniform() < (prob - 1.0f)) ? 2 : 1;
      }

      if(numSteps > 1) {
	  switch(mode) {
	  case MODE_FORWARD:
	      channel_index[y] += stepsize;
	      channel_index[y] %= numSteps;
	      break;
	  case MODE_BACKWARD:
	      channel_index[y] -= stepsize;
	      channel_index[y] = modN(channel_index[y], numSteps);
	      break;
	  case MODE_ALTERNATING:
	      if (direction[y]) {
		  channel_index[y] += stepsize;
		  if (channel_index[y] >= numSteps - 1) {
		      //the second part is zero when we end right at the last step or 1 when we moved past it because of skipping
		      //use that to correct for position for skipping
		      channel_index[y] = numSteps - 1 - (channel_index[y] % (numSteps - 1));
		      direction[y] = !direction[y];
		  }
	      }
	      else {
		  channel_index[y] -= stepsize;
		  if (channel_index[y] <= 0) {
		      channel_index[y] = -1 * channel_index[y];
		      direction[y] = !direction[y];
		  }
	      }
	      break;
	  case MODE_RANDOM:
	      channel_index[y] = rand() % numSteps;
	      break;
	  }
      }
      else
	  channel_index[y] = 0;

      stepLights[y][channel_index[y]] = 1.0;
    }

    // Outputs
    rowValue[y] = (params[ROW1_PARAM + channel_index[y] + y * 8].value) * params[CHANNEL_RANGE_PARAM + y].value;
    outputs[ROW1_OUTPUT + y].value = (running || manualStepSelect) ? outputs[ROW1_OUTPUT + y].value = rowValue[y] : 0.0f;

    // steplights
    for (int i = 0; i < 8; i++) {
      //stepLights[y][i] -= stepLights[y][i] / lightLambda / engineGetSampleRate();
      stepLights[y][i] = (i == channel_index[y]) ? 1.0 : 0.0;
      lights[CHANNEL_LIGHTS + i + 8*y].value = stepLights[y][i];
    }
  }
  resetLight -= resetLight / lightLambda / engineGetSampleRate();
  lights[RESET_LIGHT].value = resetLight;
}

struct QuadSeqWidget : ModuleWidget {
	QuadSeqWidget(QuadSeq *module);
};

QuadSeqWidget::QuadSeqWidget(QuadSeq *module) : ModuleWidget(module) {
  box.size = Vec(15*22, 380);

  {
    SVGPanel *panel = new SVGPanel();
    panel->box.size = box.size;
    panel->setBackground(SVG::load(assetPlugin(plugin, "res/QuadSeq.svg")));
    addChild(panel);
  }

  // addChild(Widget::create<ScrewSilver>(Vec(5, 0)));
  // addChild(Widget::create<ScrewSilver>(Vec(box.size.x-20, 0)));
  // addChild(Widget::create<ScrewSilver>(Vec(5, 365)));
  // addChild(Widget::create<ScrewSilver>(Vec(box.size.x-20, 365)));

  //original 56
  addParam(ParamWidget::create<RoundBlackKnob>(Vec(47.5, 18), module, QuadSeq::CLOCK_PARAM, -2.0, 6.0, 2.0));

  addParam(ParamWidget::create<LEDBezel>(Vec(11, 62), module, QuadSeq::RUN_PARAM, 0.0, 1.0, 0.0));
  addChild(ModuleLightWidget::create<BigLight<GreenLight>>(Vec(13.5, 64.5), module, QuadSeq::RUNNING_LIGHT));
  addParam(ParamWidget::create<LEDBezel>(Vec(51, 62), module, QuadSeq::RESET_PARAM, 0.0, 1.0, 0.0));
  addChild(ModuleLightWidget::create<BigLight<GreenLight>>(Vec(53.5, 64.5), module, QuadSeq::RESET_LIGHT));

  addInput(Port::create<PJ301MPort>(Vec(10, 20.5), Port::INPUT, module, QuadSeq::CLOCK_INPUT));
  addInput(Port::create<PJ301MPort>(Vec(10, 101), Port::INPUT, module, QuadSeq::EXT_CLOCK_INPUT));
  addInput(Port::create<PJ301MPort>(Vec(50, 101), Port::INPUT, module, QuadSeq::RESET_INPUT));

  for (int i=0;i<NUM_CHANNELS;i++) {
    addParam(ParamWidget::create<RoundBlackSnapKnob>(Vec(109 + i*55, 28), module, QuadSeq::CHANNEL_STEPS_PARAM + i, 1.0f, 8.0f, 8.0f));
    addParam(ParamWidget::create<Trimpot>(Vec(115 + i*55, 63), module, QuadSeq::CHANNEL_RANGE_PARAM + i, 0.0f, 1.0f, 0.5f));
    addParam(ParamWidget::create<Trimpot>(Vec(115 + i*55, 85), module, QuadSeq::CHANNEL_PROB_PARAM + i, 0.0f, 1.0f, 0.5f));
    addParam(ParamWidget::create<SnapTrimpot>(Vec(115 + i*55, 107), module, QuadSeq::CHANNEL_MODE_PARAM + i, 0.0f, 4.0f, 0.0f));

    addInput(Port::create<PJ301MPort>(Vec(18 + i*38, 350), Port::INPUT, module, QuadSeq::CHANNEL_CLOCK_INPUT + i));
    addOutput(Port::create<PJ301MPort>(Vec(172 + i*38, 350), Port::OUTPUT, module, QuadSeq::ROW1_OUTPUT + i));
    for (int y = 0; y < 8; y++) {
      addParam(ParamWidget::create<Knob29>(Vec(18 + y*38 ,152 + i*41), module, QuadSeq::ROW1_PARAM + y + i*8, 0.0, 10.0, 0.0));
      addChild(ModuleLightWidget::create<SmallLight<RedLight>>(Vec(28 + y*38, 183 + i*41), module, QuadSeq::CHANNEL_LIGHTS + y + i*8));
    }
  }

  for (int y = 0; y < 8; y++) {
      addParam(ParamWidget::create<LEDButton>(Vec(22.5 + y * 38, 316), module, QuadSeq::STEP_SELECT_PARAM + y, 0.0f, 1.0f, 0.0f));
  }
}

Model *modelQuadSeq = Model::create<QuadSeq, QuadSeqWidget>("Aepelzens Modules", "QuadSeq", "Quad Sequencer", SEQUENCER_TAG);

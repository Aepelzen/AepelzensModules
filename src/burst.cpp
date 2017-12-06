#include "aepelzen.hpp"
#include "dsp/digital.hpp"

#define MAX_REPS 8
#define MAX_TIME 1

struct Burst : Module
{
  enum ParamIds
    {
      BUTTON_PARAM,
      TIME_PARAM,
      REP_PARAM,
      ACCEL_PARAM,
      JITTER_PARAM,
      CV_MODE_PARAM,
      GATE_MODE_PARAM,
      REP_ATT_PARAM,
      TIME_ATT_PARAM,
      NUM_PARAMS = TIME_ATT_PARAM
    };
  enum InputIds
    {
      GATE_INPUT,
      CLOCK_INPUT,
      REP_INPUT,
      TIME_INPUT,
      NUM_INPUTS = TIME_INPUT
    };
  enum OutputIds
    {
      GATE_OUTPUT,
      EOC_OUTPUT,
      CV_OUTPUT,
      NUM_OUTPUTS = CV_OUTPUT
    };

  enum LightIds
    {
      NUM_LIGHTS
    };

  int panel = 0;
  float timeParam = 0;
  float clockedTimeParam = 0;
  float pulseParam = 4;
  float timer = 0;
  float seconds = 0;
  int pulseCount = 0;
  int pulses = 4;
  float delta = 0;

  SchmittTrigger m_buttonTrigger;
  SchmittTrigger gateTrigger;;
  SchmittTrigger clockTrigger;

  PulseGenerator outPulse;
  PulseGenerator eocPulse;

  //toggle for every received clock tick
  float clockTimer = 0;
  float lastClockTime = 0;
  float gateOutLength = 0.01;

  void step() override;

  Burst() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}

  void reset() override
  {
    onSampleRateChange();
  }

  void onSampleRateChange() override {delta = 1.0/engineGetSampleRate();}

  void randomize() override
  {

  }

  json_t *toJson() override
  {
    json_t *rootJ = json_object();
    json_object_set_new(rootJ, "panel", json_integer(panel));
    return rootJ;
  }
  void fromJson(json_t *rootJ) override
  {
    json_t *panelJ = json_object_get(rootJ, "panel");
    if (panelJ)
      panel = json_integer_value(panelJ);
  }
};

void Burst::step()
{
  //ModuleWidget::step();
  float schmittValue = gateTrigger.process(inputs[GATE_INPUT].value);

  //seconds = params[TIME_PARAM].value + (inputs[TIME_INPUT].value / 20.0);
  float accel = params[ACCEL_PARAM].value;
  float jitter = params[JITTER_PARAM].value;
  float randomDelta = 0;

  timeParam = clampf(params[TIME_PARAM].value + (params[TIME_ATT_PARAM].value * inputs[TIME_INPUT].value / 5.0 * MAX_TIME),0.0, MAX_TIME);
  pulseParam = clampf(params[REP_PARAM].value + (inputs[REP_INPUT].value * params[REP_ATT_PARAM].value /5.0 * MAX_REPS), 0.0, MAX_REPS);

  if (inputs[CLOCK_INPUT].active) {
    clockTimer += delta;
    if( clockTrigger.process(inputs[CLOCK_INPUT].value) ) {
      timeParam = params[TIME_PARAM].value;
      int mult = (int)(timeParam*8 - 4);
      //smooth clock (over 8 pulses) to reduce sensitivity
      // float clockDelta = clockTimer - lastClockTime;
      // clockTimer -= clockTimer - (clockDelta/8);
      lastClockTime = clockTimer;

      timeParam = clockTimer * pow(2, mult);
      clockedTimeParam = timeParam;
      clockTimer = 0;
    }
    timeParam = clockedTimeParam;
  }

  if( timer > seconds && pulseCount < pulses ) {
    pulseCount ++;
    timer = 0.0;

    seconds = timeParam;

    if(accel > 0) {
      seconds = timeParam/pow(accel,pulseCount);
    }

    if(jitter > 0) {
      randomDelta = randomf() * jitter * seconds;
      if (randomf() > 0.5) {
	seconds = seconds + randomDelta;
      }
      else {
	seconds = seconds - randomDelta;
      }
    }

    if (pulseCount == pulses) {
      eocPulse.trigger(0.01);
    }
    gateOutLength = (params[GATE_MODE_PARAM].value) ? 0.01 : seconds/2;
    outPulse.trigger(gateOutLength);
  }

  if (schmittValue || m_buttonTrigger.process(params[BUTTON_PARAM].value)) {
    pulseCount = 0;
    timer = 0.0;
    //outPulse.trigger(0.01);
    outPulse.trigger(gateOutLength);
    seconds = timeParam;
    pulses = pulseParam;
  }

    //manual trigger
  // if (m_buttonTrigger.process(params[BUTTON_PARAM].value)) {
  //   outPulse.trigger(0.01);
  // }

  timer += delta;
  outputs[GATE_OUTPUT].value = outPulse.process(delta) ? 10.0 : 0.0;
  outputs[EOC_OUTPUT].value = eocPulse.process(delta) ? 10.0 : 0.0;
}


BurstWidget::BurstWidget()
{
  auto *module = new Burst();
  setModule(module);
  //box.size = Vec(6 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
  box.size = Vec(6 * 15, RACK_GRID_HEIGHT);

  panel = new SVGPanel();
  panel->box.size = box.size;
  panel->setBackground(SVG::load(assetPlugin(plugin, "res/Burst.svg")));
  addChild(panel);

  // addChild(createScrew<ScrewSilver>(Vec(15, 0)));
  // addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 0)));
  // addChild(createScrew<ScrewSilver>(Vec(15, 365)));
  // addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 365)));

  //note: SmallKnob size = 28px, Trimpot = 17 px
  //addParam(createParam<LEDBezel>(Vec(30, 105), module, Burst::BUTTON_PARAM, 0.0, 1.0, 0.0));
  addParam(createParam<CKD6>(Vec(30, 105), module, Burst::BUTTON_PARAM, 0.0, 1.0, 0.0));
  //addParam(createParam<RoundSmallBlackKnob>(Vec(35, 50), module, Burst::REP_PARAM, 1, 8, 4));
  addParam(createParam<Davies1900hLargeBlackKnob>(Vec(18, 30), module, Burst::REP_PARAM, 0, 8, 4));
  addParam(createParam<RoundSmallBlackKnob>(Vec(10, 150), module, Burst::TIME_PARAM, 0.02, 1, 0.5));
  addParam(createParam<RoundSmallBlackKnob>(Vec(52, 150), module, Burst::ACCEL_PARAM, 1.0, 2.0, 1.0));
  addParam(createParam<RoundSmallBlackKnob>(Vec(10, 195), module, Burst::JITTER_PARAM, 0.0, 1.0, 0.0));
  addParam(createParam<RoundSmallBlackKnob>(Vec(52, 195), module, Burst::CV_MODE_PARAM, 0, 8, 0));

  addParam(createParam<Trimpot>(Vec(15.5, 240), module, Burst::REP_ATT_PARAM, -1.0, 1.0, 0.0));
  addParam(createParam<Trimpot>(Vec(54, 240), module, Burst::TIME_ATT_PARAM, -1.0, 1.0, 0.0));
  addInput(createInput<PJ301MPort>(Vec(13, 265), module, Burst::REP_INPUT));;
  addInput(createInput<PJ301MPort>(Vec(50, 265), module, Burst::TIME_INPUT));;

  addInput(createInput<PJ301MPort>(Vec(5, 305), module, Burst::GATE_INPUT));;
  addInput(createInput<PJ301MPort>(Vec(60, 305), module, Burst::CLOCK_INPUT));;
  addParam(createParam<CKSS>(Vec(38, 300), module, Burst::GATE_MODE_PARAM, 0.0, 1.0, 0.0));

  addOutput(createOutput<PJ301MPort>(Vec(5,335), module, Burst::CV_OUTPUT));
  addOutput(createOutput<PJ301MPort>(Vec(32.5,335), module, Burst::EOC_OUTPUT));
  addOutput(createOutput<PJ301MPort>(Vec(60,335), module, Burst::GATE_OUTPUT));
}

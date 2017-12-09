#include "aepelzen.hpp"
#include "dsp/digital.hpp"

struct Folder : Module
{
  enum ParamIds
    {
      GAIN_PARAM,
      GAIN_ATT_PARAM,
      SYM_PARAM,
      SYM_ATT_PARAM,
      STAGE_PARAM,
      NUM_PARAMS
    };
  enum InputIds
    {
      GATE_INPUT,
      GAIN_INPUT,
      SYM_INPUT,
      NUM_INPUTS
    };
  enum OutputIds
    {
      GATE_OUTPUT,
      NUM_OUTPUTS
    };

  enum LightIds
    {
      NUM_LIGHTS
    };

  void step() override;

  Folder() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}

  void reset() override
  {
    onSampleRateChange();
  }

  void onSampleRateChange() override {}

  void randomize() override  {}
  // json_t *toJson() override {}
  // void fromJson(json_t *rootJ) override {}
};

float fold(float in, float threshold) {
  float out;
  if (in>threshold || in<-threshold) {
    out = clampf(fabs(fabs(fmod(in - threshold, threshold*4)) - threshold*2) - threshold, -5.0,5.0); 
  }
  else {
    out = in;
  }
  return out;
}

float fold3(float in, float t) {
  float out = 0;
  if(in>t) {
    out = t - (in - t);
  }
  else if (in < -t) {
    out = -t + (-t - in);
  }
  else {
    out = in;
  }
  return out;
}

float in, out, gain, sym;
float threshold = 1.0;

void Folder::step()
{
  gain = clampf(params[GAIN_PARAM].value + (inputs[GAIN_INPUT].value * 2 * params[GAIN_ATT_PARAM].value), 0.0,10.0);
  sym = clampf(params[SYM_PARAM].value + inputs[SYM_INPUT].value * params[SYM_ATT_PARAM].value, -1.0, 1.0);
  in = (inputs[GATE_INPUT].value/5.0 + sym) * gain;
  
  out = in;
  int stages = (int)(params[STAGE_PARAM].value)*2;
  for (int i=0;i<stages;i++) {
    out = fold3(out, threshold);
  }

  out = tanh(out);
  //out = clampf(out, -1.0, 1.0);
  outputs[GATE_OUTPUT].value = out * 5.0;
  return;
}

FolderWidget::FolderWidget()
{
  auto *module = new Folder();
  setModule(module);
  //box.size = Vec(6 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
  box.size = Vec(4 * 15, RACK_GRID_HEIGHT);

  panel = new SVGPanel();
  panel->box.size = box.size;
  panel->setBackground(SVG::load(assetPlugin(plugin, "res/Folder.svg")));
  addChild(panel);

  // addChild(createScrew<ScrewSilver>(Vec(15, 0)));
  // addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 0)));
  // addChild(createScrew<ScrewSilver>(Vec(15, 365)));
  // addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 365)));

  //note: SmallKnob size = 28px, Trimpot = 17 px
  addParam(createParam<BefacoSwitch>(Vec(16, 40), module, Folder::STAGE_PARAM, 1, 3, 2));
  addParam(createParam<RoundSmallBlackKnob>(Vec(16, 80), module, Folder::GAIN_PARAM, 0.0, 10.0, 1.0));
  addParam(createParam<Trimpot>(Vec(21.5, 120), module, Folder::GAIN_ATT_PARAM, -1.0, 1.0, 0));
  
  addParam(createParam<RoundSmallBlackKnob>(Vec(16, 200), module, Folder::SYM_PARAM, -1.0, 1.0, 0.0));
  addParam(createParam<Trimpot>(Vec(21.5, 240), module, Folder::SYM_ATT_PARAM, -1.0, 1.0, 0.0));  


  addInput(createInput<PJ301MPort>(Vec(3, 335), module, Folder::GATE_INPUT));;
  addInput(createInput<PJ301MPort>(Vec(3, 290), module, Folder::GAIN_INPUT));;
  addInput(createInput<PJ301MPort>(Vec(30, 290), module, Folder::SYM_INPUT));;  
  addOutput(createOutput<PJ301MPort>(Vec(30,335), module, Folder::GATE_OUTPUT));
}

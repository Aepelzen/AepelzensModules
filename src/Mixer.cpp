#include "aepelzen.hpp"
#include "AeFilter.hpp"

struct Mixer : Module {
    enum ParamIds {
	GAIN_PARAM,
	MUTE_PARAM,
	EQ_LOW_PARAM,
	EQ_MID_PARAM,
	EQ_HIGH_PARAM,
	NUM_PARAMS
    };
    enum InputIds {
	CH1_INPUT,
	NUM_INPUTS
    };
    enum OutputIds {
	L_OUTPUT,
	NUM_OUTPUTS
    };
    enum LightIds {
	NUM_LIGHTS
    };

    AeEqualizer eqLow;
    AeEqualizer eqMid;
    AeEqualizer eqHigh;

    Mixer() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
    void step() override;
};


void Mixer::step() {

    float in = inputs[CH1_INPUT].value /5.0f;

    float gain = params[GAIN_PARAM].value;
    float lowGain = params[EQ_LOW_PARAM].value;
    float midGain = params[EQ_MID_PARAM].value;
    float highGain = params[EQ_HIGH_PARAM].value;

    eqLow.setParams(35.0f, 0.35f, lowGain, AeEQType::AePEAKINGEQ);
    eqMid.setParams(1000.0f, 0.8f, midGain, AeEQType::AePEAKINGEQ);
    eqHigh.setParams(7000.0f, 0.2f, highGain, AeEQType::AePEAKINGEQ);

    float out = eqLow.process(in);
    out = eqMid.process(out);
    out = eqHigh.process(out);

    //outputs[L_OUTPUT].value = clamp(out, -5.0f, 5.0f);
    outputs[L_OUTPUT].value = tanh(out) * 5.0f * gain;;
}


struct MixerWidget : ModuleWidget {
    MixerWidget(Mixer *module) : ModuleWidget(module) {
	setPanel(SVG::load(assetPlugin(plugin, "res/Mixer.svg")));

	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	addParam(ParamWidget::create<BefacoTinyKnob>(Vec(10, 120), module, Mixer::EQ_HIGH_PARAM, -20.0f, 12.0f, 0.0f));
	addParam(ParamWidget::create<BefacoTinyKnob>(Vec(10, 160), module, Mixer::EQ_MID_PARAM, -32.0f, 12.0f, 0.0f));
	addParam(ParamWidget::create<BefacoTinyKnob>(Vec(10, 200), module, Mixer::EQ_LOW_PARAM, -32.0f, 16.0f, 0.0f));

	addParam(ParamWidget::create<BefacoSwitch>(Vec(10, 230), module, Mixer::MUTE_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BefacoTinyRedKnob>(Vec(10, 275), module, Mixer::GAIN_PARAM, 0.0f, 1.0f, 1.0f));

	addInput(Port::create<PJ301MPort>(Vec(10, 310), Port::INPUT, module, Mixer::CH1_INPUT));
	addOutput(Port::create<PJ301MPort>(Vec(40, 340), Port::OUTPUT, module, Mixer::L_OUTPUT));

    }
};

Model *modelMixer = Model::create<Mixer, MixerWidget>("Aepelzens Modules", "Mixer", "Mixer", MIXER_TAG);

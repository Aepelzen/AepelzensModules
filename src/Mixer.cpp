#include "aepelzen.hpp"
#include "AeFilter.hpp"
#include "dsp/vumeter.hpp"

#define NUM_CHANNELS 6

struct Mixer : Module {
    enum ParamIds {
	MASTER_GAIN_PARAM,
	GAIN_PARAM,
	MASTER_EQ_LOW_PARAM,
	MASTER_EQ_MID_PARAM,
	MASTER_EQ_HIGH_PARAM,
	MUTE_PARAM = GAIN_PARAM + NUM_CHANNELS,
	EQ_LOW_PARAM = MUTE_PARAM + NUM_CHANNELS,
	EQ_MID_PARAM = EQ_LOW_PARAM + NUM_CHANNELS,
	EQ_HIGH_PARAM = EQ_MID_PARAM + NUM_CHANNELS,
	PAN_PARAM = EQ_HIGH_PARAM + NUM_CHANNELS,
	AUX1_PARAM = PAN_PARAM + NUM_CHANNELS,
	AUX2_PARAM = AUX1_PARAM + NUM_CHANNELS,
	NUM_PARAMS = AUX2_PARAM + NUM_CHANNELS
    };
    enum InputIds {
	CH1_INPUT,
	CH1_GAIN_INPUT = CH1_INPUT + NUM_CHANNELS,
	CH1_PAN_INPUT = CH1_GAIN_INPUT + NUM_CHANNELS,
	AUX1_L_INPUT = CH1_GAIN_INPUT + NUM_CHANNELS,

	AUX1_R_INPUT,
	AUX2_L_INPUT,
	AUX2_R_INPUT,
	NUM_INPUTS
    };
    enum OutputIds {
	L_OUTPUT,
	R_OUTPUT,
	AUX1_L_OUTPUT,
	AUX1_R_OUTPUT,
	AUX2_L_OUTPUT,
	AUX2_R_OUTPUT,
	NUM_OUTPUTS
    };
    enum LightIds {
	METER_L_LIGHT,
	METER_R_LIGHT = METER_L_LIGHT + 6,
	NUM_LIGHTS = METER_R_LIGHT + 6
    };

    Mixer() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {

	for(int i=0;i<NUM_CHANNELS;i++) {
	    channels[i].hp.setCutoff(35.0f, 0.8f, AeFilterType::AeHIGHPASS);
	    channels[i].hs.setParams(12000.0f, 0.6f, -6.0f, AeEQType::AeHIGHSHELVE);
	}

	meter.dBInterval = 10.0f;
    }

    struct mixerChannel {
	AeEqualizer eqLow;
	AeEqualizer eqMid;
	AeEqualizer eqHigh;

	AeFilter hp;
	AeEqualizer hs;
    };

    mixerChannel channels[NUM_CHANNELS];
    VUMeter meter;

    void step() override;
};


void Mixer::step() {

    float outL = 0.0f;
    float outR = 0.0f;
    float aux1L = 0.0f;
    float aux1R = 0.0f;
    float aux2L = 0.0f;
    float aux2R = 0.0f;

    float masterGain = params[MASTER_GAIN_PARAM].value;
    masterGain = pow(10, masterGain/20);

    float aux1LIn = inputs[AUX1_L_INPUT].normalize(0.0f);
    float aux1RIn = inputs[AUX1_R_INPUT].normalize(0.0f);
    float aux2LIn = inputs[AUX2_L_INPUT].normalize(0.0f);
    float aux2RIn = inputs[AUX2_R_INPUT].normalize(0.0f);

    for(int i=0;i<NUM_CHANNELS;i++) {
	float in = inputs[CH1_INPUT + i].value /5.0f;

	float gain = params[GAIN_PARAM + i].value * inputs[CH1_GAIN_INPUT + i].normalize(10.0f) / 10.0f;
	float pan = params[PAN_PARAM + i].value;
	float lowGain = params[EQ_LOW_PARAM + i].value;
	float midGain = params[EQ_MID_PARAM + i].value;
	float highGain = params[EQ_HIGH_PARAM + i].value;

	channels[i].eqLow.setParams(125.0f, 0.45f, lowGain, AeEQType::AeLOWSHELVE);
	channels[i].eqMid.setParams(1200.0f, 0.52f, midGain, AeEQType::AePEAKINGEQ);
	//eqHigh.setParams(7000.0f, 0.25f, highGain, AeEQType::AePEAKINGEQ);
	channels[i].eqHigh.setParams(1800.0f, 0.42f, highGain, AeEQType::AeHIGHSHELVE);

	float out = channels[i].eqLow.process(in);
	out = channels[i].eqMid.process(out);
	out = channels[i].eqHigh.process(out);
	out = channels[i].hp.process(out);
	out = channels[i].hs.process(out);

	float leftGain = (pan < 0) ? gain : gain * (1 - pan);
	float rightGain = (pan > 0) ? gain : gain * (1 + pan);

	//outputs
	out = tanh(out) * 5.0f;
	outL += out * leftGain;
	outR += out * rightGain;
	aux1L += out * leftGain * params[AUX1_PARAM + i].value;
	aux1R += out * rightGain * params[AUX1_PARAM + i].value;
	aux2L += out * leftGain * params[AUX2_PARAM + i].value;
	aux2R += out * rightGain * params[AUX2_PARAM + i].value;
    }

    outL = (outL + aux1LIn + aux2LIn) * masterGain;
    outR = (outR + aux1RIn + aux2RIn) * masterGain;
    outputs[L_OUTPUT].value = outL;
    outputs[R_OUTPUT].value = outR;
    outputs[AUX1_L_OUTPUT].value = aux1L;
    outputs[AUX1_R_OUTPUT].value = aux1R;
    outputs[AUX2_L_OUTPUT].value = aux2L;
    outputs[AUX2_R_OUTPUT].value = aux2R;

    //lights
    for (int i = 0; i < 6; i++){
	meter.setValue(outL / 5.0f);
	lights[METER_L_LIGHT + i].setBrightnessSmooth(meter.getBrightness(i));
	meter.setValue(outR / 5.0f);
	lights[METER_R_LIGHT + i].setBrightnessSmooth(meter.getBrightness(i));
    }
}

struct MixerWidget : ModuleWidget {
    MixerWidget(Mixer *module) : ModuleWidget(module) {
	setPanel(SVG::load(assetPlugin(plugin, "res/Mixer.svg")));

	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	for(int i=0;i<NUM_CHANNELS;i++) {
	    addParam(ParamWidget::create<BefacoTinyWhiteKnob>(Vec(100 + i * 48, 10), module, Mixer::AUX1_PARAM + i, 0.0f, 1.0f, 0.0f));
	    addParam(ParamWidget::create<BefacoTinyWhiteKnob>(Vec(100 + i * 48, 57), module, Mixer::AUX2_PARAM + i, 0.0f, 1.0f, 0.0f));
	    addParam(ParamWidget::create<BefacoTinyWhiteKnob>(Vec(100 + i * 48, 104), module, Mixer::PAN_PARAM + i, -1.0f, 1.0f, 0.0f));
	    addParam(ParamWidget::create<BefacoTinyDarkKnob>(Vec(100 + i * 48, 151), module, Mixer::EQ_HIGH_PARAM + i, -15.0f, 15.0f, 0.0f));
	    addParam(ParamWidget::create<BefacoTinyDarkKnob>(Vec(100 + i * 48, 198), module, Mixer::EQ_MID_PARAM + i, -12.5f, 12.5f, 0.0f));
	    addParam(ParamWidget::create<BefacoTinyDarkKnob>(Vec(100 + i * 48, 245), module, Mixer::EQ_LOW_PARAM + i, -20.0f, 20.0f, 0.0f));

	    addParam(ParamWidget::create<BefacoPush>(Vec(103 + i * 48, 290), module, Mixer::MUTE_PARAM + i, 0.0f, 1.0f, 0.0f));
	    addParam(ParamWidget::create<BefacoTinyRedKnob>(Vec(100 + i * 48, 330), module, Mixer::GAIN_PARAM + i, 0.0f, 1.0f, 1.0f));

	    addInput(Port::create<PJ301MPort>(Vec(5, 25 + i * 30), Port::INPUT, module, Mixer::CH1_INPUT + i));
	    addInput(Port::create<PJ301MPort>(Vec(35, 25 + i * 30), Port::INPUT, module, Mixer::CH1_GAIN_INPUT + i));
	    addInput(Port::create<PJ301MPort>(Vec(65, 25 + i * 30), Port::INPUT, module, Mixer::CH1_PAN_INPUT + i));
	}

	addParam(ParamWidget::create<Davies1900hLargeRedKnob>(Vec(380, 310), module, Mixer::MASTER_GAIN_PARAM, -60.0f, 0.0f, -20.0f));

	addParam(ParamWidget::create<Davies1900hWhiteKnob>(Vec(389, 143), module, Mixer::MASTER_EQ_HIGH_PARAM, -10.0f, 10.0f, 0.0f));
	addParam(ParamWidget::create<Davies1900hWhiteKnob>(Vec(389, 198), module, Mixer::MASTER_EQ_MID_PARAM, -10.0f, 10.0f, 0.0f));
	addParam(ParamWidget::create<Davies1900hWhiteKnob>(Vec(389, 253), module, Mixer::MASTER_EQ_LOW_PARAM, -10.0f, 10.0f, 0.0f));	
	//meter
	for(int i=0;i<6;i++) {
	    addChild(ModuleLightWidget::create<MediumLight<RedLight>>(Vec(390, 60 + i * 12), module, Mixer::METER_L_LIGHT + i));
	    addChild(ModuleLightWidget::create<MediumLight<RedLight>>(Vec(410, 60 + i * 12), module, Mixer::METER_R_LIGHT + i));
	}

	addOutput(Port::create<PJ301MPort>(Vec(380, 10), Port::OUTPUT, module, Mixer::L_OUTPUT));
	addOutput(Port::create<PJ301MPort>(Vec(410, 10), Port::OUTPUT, module, Mixer::R_OUTPUT));

	addOutput(Port::create<PJ301MPort>(Vec(10, 234), Port::OUTPUT, module, Mixer::AUX1_L_OUTPUT));
	addOutput(Port::create<PJ301MPort>(Vec(40, 234), Port::OUTPUT, module, Mixer::AUX1_R_OUTPUT));
	addInput(Port::create<PJ301MPort>(Vec(10, 264), Port::INPUT, module, Mixer::AUX1_L_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(40, 264), Port::INPUT, module, Mixer::AUX1_R_INPUT));

	addOutput(Port::create<PJ301MPort>(Vec(10, 310), Port::OUTPUT, module, Mixer::AUX2_L_OUTPUT));
	addOutput(Port::create<PJ301MPort>(Vec(40, 310), Port::OUTPUT, module, Mixer::AUX2_R_OUTPUT));
	addInput(Port::create<PJ301MPort>(Vec(10, 340), Port::INPUT, module, Mixer::AUX2_L_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(40, 340), Port::INPUT, module, Mixer::AUX2_R_INPUT));

    }
};

Model *modelMixer = Model::create<Mixer, MixerWidget>("Aepelzens Modules", "Mixer", "Mixer", MIXER_TAG);

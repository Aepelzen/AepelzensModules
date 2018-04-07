#include "aepelzen.hpp"
#include "dsp/digital.hpp"
#include "dsp/samplerate.hpp"

#define BUF_LEN 32
#define FOLDER_SAMPLE_RATE 192000

struct Folder : Module {
    enum ParamIds {
	GAIN_PARAM,
	GAIN_ATT_PARAM,
	SYM_PARAM,
	SYM_ATT_PARAM,
	STAGE_PARAM,
	NUM_PARAMS
    };
    enum InputIds {
	GATE_INPUT,
	GAIN_INPUT,
	SYM_INPUT,
	NUM_INPUTS
    };
    enum OutputIds {
	GATE_OUTPUT,
	NUM_OUTPUTS
    };

    enum LightIds {
	NUM_LIGHTS
    };

    void step() override;

    Folder() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	convUp.setRates(engineGetSampleRate(),FOLDER_SAMPLE_RATE);
	convDown.setRates(FOLDER_SAMPLE_RATE, engineGetSampleRate());
    }

    void onSampleRateChange() override {
	convUp.setRates(engineGetSampleRate(),FOLDER_SAMPLE_RATE);
	convDown.setRates(FOLDER_SAMPLE_RATE, engineGetSampleRate());
    }

    void randomize() override  {}

    json_t *toJson() override {
	json_t *rootJ = json_object();
	json_object_set_new(rootJ, "alternativeMode", json_boolean(alternativeMode));
	return rootJ;
    }

    void fromJson(json_t *rootJ) override {
	json_t *modeJ = json_object_get(rootJ, "alternativeMode");
	if(modeJ) {
	    alternativeMode = json_boolean_value(modeJ);
	}
    }

    float in, out, gain, sym;
    float threshold = 1.0;

    bool alternativeMode = false;

    SampleRateConverter<1> convUp;
    SampleRateConverter<1> convDown;

    int frame = 0;
    //smallest supported sampleRate in Rack is 44100 (upsample ratio = 4.35)
    Frame<1> in_buffer[BUF_LEN] = {};
    Frame<1> out_buffer[5*BUF_LEN] = {};
    Frame<1> folded_buffer[BUF_LEN] = {};

    float fold(float in, float threshold) {
	float out;
	if (in>threshold || in<-threshold) {
	    out = clamp(fabs(fabs(fmod(in - threshold, threshold*4)) - threshold*2) - threshold, -5.0f,5.0f);
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

};

void Folder::step() {
    gain = clamp(params[GAIN_PARAM].value + (inputs[GAIN_INPUT].value * params[GAIN_ATT_PARAM].value), 0.0f,14.0f);
    sym = clamp(params[SYM_PARAM].value + inputs[SYM_INPUT].value/5.0 * params[SYM_ATT_PARAM].value, -1.0f, 1.0f);
    in = (inputs[GATE_INPUT].value/5.0 + sym) * gain;

    if(++frame >= BUF_LEN) {
	//upsampling
	int outLen = 5 *BUF_LEN;
	int inLen = BUF_LEN;
	convUp.process(in_buffer, &inLen, out_buffer, &outLen);

	//fold
	for(int i=0;i<outLen;i++) {
	    if(!alternativeMode) {
		int stages = (int)(params[STAGE_PARAM].value)*2;
		for (int y=0;y<stages;y++) {
		    out_buffer[i].samples[0] = fold3(out_buffer[i].samples[0], threshold);
		}
	    }
	    else {
		out_buffer[i].samples[0] = fold(out_buffer[i].samples[0], threshold);
	    }
	    out_buffer[i].samples[0] = tanh(out_buffer[i].samples[0]);
	}

	//downSampling
	int foldedLen = BUF_LEN;
	convDown.process(out_buffer, &outLen, folded_buffer, &foldedLen);
	frame = 0;
    }

    in_buffer[frame].samples[0] = in;
    outputs[GATE_OUTPUT].value = folded_buffer[frame].samples[0] * 5.0;
}


struct FolderWidget : ModuleWidget {
    FolderWidget(Folder *module);
    Menu *createContextMenu() override;
};

FolderWidget::FolderWidget(Folder *module) : ModuleWidget(module) {
    box.size = Vec(4 * 15, RACK_GRID_HEIGHT);

    SVGPanel* panel = new SVGPanel();
    panel->box.size = box.size;
    panel->setBackground(SVG::load(assetPlugin(plugin, "res/Folder.svg")));
    addChild(panel);

    //note: SmallKnob size = 28px, Trimpot = 17 px
    addParam(ParamWidget::create<BefacoSwitch>(Vec(16, 50), module, Folder::STAGE_PARAM, 1, 3, 2));
    //addParam(ParamWidget::create<CKSSThreeH>(Vec(16, 50), module, Folder::STAGE_PARAM, 1, 3, 2));
    addParam(ParamWidget::create<RoundBlackKnob>(Vec(16, 100), module, Folder::GAIN_PARAM, 0.0, 14.0, 1.0));
    addParam(ParamWidget::create<Trimpot>(Vec(21.5, 145), module, Folder::GAIN_ATT_PARAM, -1.0, 1.0, 0));
    addParam(ParamWidget::create<RoundBlackKnob>(Vec(16, 185), module, Folder::SYM_PARAM, -1.0, 1.0, 0.0));
    addParam(ParamWidget::create<Trimpot>(Vec(21.5, 230), module, Folder::SYM_ATT_PARAM, -1.0, 1.0, 0.0));

    addInput(Port::create<PJ301MPort>(Vec(3, 320), Port::INPUT, module, Folder::GATE_INPUT));;
    addInput(Port::create<PJ301MPort>(Vec(3, 276), Port::INPUT, module, Folder::GAIN_INPUT));;
    addInput(Port::create<PJ301MPort>(Vec(30, 276), Port::INPUT, module, Folder::SYM_INPUT));;
    addOutput(Port::create<PJ301MPort>(Vec(30,320), Port::OUTPUT, module, Folder::GATE_OUTPUT));
}

struct FolderMenuItem : MenuItem {
    Folder *module;
    void onAction(EventAction &e) override {
	module->alternativeMode ^= true;
    }
    void step() override {
	rightText = (module->alternativeMode) ? "✔" : "";
	MenuItem::step();
    }
};

Menu *FolderWidget::createContextMenu() {
    Menu *menu = ModuleWidget::createContextMenu();

    Folder *folder = dynamic_cast<Folder*>(module);
    assert(folder);

    menu->addChild(construct<MenuEntry>());
    menu->addChild(construct<FolderMenuItem>(&FolderMenuItem::text, "Alternative Folding Algorithm", &FolderMenuItem::module, folder));

    return menu;
}

Model *modelFolder = Model::create<Folder, FolderWidget>("Aepelzens Modules", "folder", "Manifold", WAVESHAPER_TAG);

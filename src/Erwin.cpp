#include "aepelzen.hpp"
#include "dsp/digital.hpp"
#include "osdialog.h"
#include <math.h>

#define NUM_CHANNELS 4
#define NUM_SCALES 16

struct Erwin : Module {
    enum ParamIds {
	CHANNEL_TRANSPOSE_PARAM,
	CHANNEL_MONITOR_PARAM = CHANNEL_TRANSPOSE_PARAM + NUM_CHANNELS,
	NOTE_PARAM,
	SELECT_PARAM = NOTE_PARAM + 12,
	NUM_PARAMS
    };
    enum InputIds {
	TRANSPOSE_INPUT,
	SEMI_INPUT,
	IN_INPUT,
	SELECT_INPUT = IN_INPUT + 4,
	NUM_INPUTS
    };
    enum OutputIds {
	OUT_OUTPUT,
	NUM_OUTPUTS = OUT_OUTPUT + 4
    };
    enum LightIds {
	NOTE_LIGHT,
	CHANNEL_MONITOR_LIGHT = NOTE_LIGHT + 24,
	NUM_LIGHTS = CHANNEL_MONITOR_LIGHT + NUM_CHANNELS
    };

    enum QModes {
	DOWN,
	UP,
	NEAREST
    };

    Erwin() : Module( NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) { reset(); };
    void step() override;
    json_t* toJson() override;
    void fromJson(json_t *rootJ) override;
    void reset() override;

    //float ratios[13] = {1.0, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3, 7.0/5, 3.0/2, 8.0/5, 5.0/3, 16.0/9, 15.0/8, 2.0};
    int mode = 0;
    bool noteState[12 * NUM_SCALES] = {};
    int octave = 0;
    int transposeOctave = 0;
    int transposeSemi = 0;
    float freq = 0.0f;

    unsigned int monitorChannel = 0;

    SchmittTrigger noteTriggers[12];
    SchmittTrigger monitorTrigger;
};

json_t* Erwin::toJson() {
    json_t *rootJ = json_object();

    // Note values
    json_t *gatesJ = json_array();
    for (int i = 0; i < 12 * NUM_SCALES; i++) {
	json_t *gateJ = json_boolean(noteState[i]);
	json_array_append_new(gatesJ, gateJ);
    }
    json_object_set_new(rootJ, "notes", gatesJ);

    //mode
    json_object_set_new(rootJ, "mode", json_integer(mode));
    return rootJ;
}

void Erwin::fromJson(json_t *rootJ) {
    // Note values
    json_t *gatesJ = json_object_get(rootJ, "notes");

    if(!gatesJ) {
	debug("Erwin: Invalid Input file");
	return;
    }

    for (unsigned int i = 0; i < json_array_size(gatesJ); i++) {
	json_t *gateJ = json_array_get(gatesJ, i);
	noteState[i] = gateJ ? json_boolean_value(gateJ) : false;
    }
    json_t *modeJ = json_object_get(rootJ, "mode");
    if(modeJ) {
	mode = json_integer_value(modeJ);
    }
}

void Erwin::reset() {
    for (int i = 0; i < 12 * NUM_SCALES; i++) noteState[i] = 0;
}

void Erwin::step() {

    //Scale selection
    int scaleOffset = clamp((int)(params[SELECT_PARAM].value + inputs[SELECT_INPUT].value * NUM_SCALES /10),0,15) * 12;
    bool* currentScale = noteState + scaleOffset;

    //limit to 1 octave
    transposeSemi = (int)round(inputs[SEMI_INPUT].value * 1.2);

    for(unsigned int y=0;y<NUM_CHANNELS;y++) {
	//normalize to first channel
	if(!inputs[IN_INPUT + y].active)
	    inputs[IN_INPUT + y].value = inputs[IN_INPUT].value;

	octave = trunc(inputs[IN_INPUT+y].value);
	freq = inputs[IN_INPUT+y].value - octave;
	//limit to 4 octaves
	transposeOctave = clamp((int)round(inputs[TRANSPOSE_INPUT].value / 2.5) + (int)round(params[CHANNEL_TRANSPOSE_PARAM + y].value),-4, 4);

	//index of the quantized note
	int index = 0;

	int semiUp = ceilN(freq * 12);
	int semiDown = (int)trunc(freq * 12);
	uint8_t stepsUp = 0;
	uint8_t stepsDown = 0;

	while(!currentScale[modN(semiUp + stepsUp,12)] && stepsUp < 12)
	    stepsUp++;
	while(!currentScale[modN(semiDown - stepsDown, 12)] && stepsDown < 12)
	    stepsDown++;

	//Reset for empty scales to avoid transposing by 1 octave
	stepsUp %= 12;
	stepsDown %= 12;

	switch(mode) {
	case QModes::UP:
	    index = semiUp + stepsUp;
	    break;
	case QModes::DOWN:
	    index = semiDown - stepsDown;
	    break;
	case QModes::NEAREST:
	    if (stepsUp < stepsDown)
		index = semiUp + stepsUp;
	    else
		index = semiDown - stepsDown;
	    break;
	}

	if(transposeSemi)
	    index += transposeSemi;

	outputs[OUT_OUTPUT + y].value = octave + index * 1/12.0 + transposeOctave;

	if(monitorChannel == y) {
	    for(int i=0;i<12;i++) {
		lights[NOTE_LIGHT + 2*i + 1].value = (modN(index,12) == i) ? 0.7f : 0.0f;
	    }
	    lights[CHANNEL_MONITOR_LIGHT + y].value =  1.0f;
	}
	else
	    lights[CHANNEL_MONITOR_LIGHT + y].value =  0.0f;
    }

    // Note buttons
    for (int i = 0; i < 12; i++) {
	if (noteTriggers[i].process(params[NOTE_PARAM + i].value)) {
	    currentScale[i] = !currentScale[i];
	}
	//lights[NOTE_LIGHT + 2*i].value = (currentScale[i] >= 1.0 && !lights[NOTE_LIGHT + 2*i + 1].value) ? 0.7 : 0;
	lights[NOTE_LIGHT + 2*i].value = (currentScale[i] >= 1.0) ? 0.7 : 0;
    }

    if(monitorTrigger.process(params[CHANNEL_MONITOR_PARAM].value)) {
	monitorChannel = (monitorChannel + 1) % 5;
	//reset lights
	for(int i=0;i<12;i++) {
	    lights[2*i + 1].value = 0.0f;
	}
    }
}

struct ErwinWidget : ModuleWidget {
    ErwinWidget(Erwin *module);
    Menu* createContextMenu() override;
};

ErwinWidget::ErwinWidget(Erwin *module) : ModuleWidget(module) {
    box.size = Vec(15*8, 380);

    {
	SVGPanel *panel = new SVGPanel();
	panel->box.size = box.size;
	panel->setBackground(SVG::load(assetPlugin(plugin,"res/Erwin.svg")));
	addChild(panel);
    }

    addChild(Widget::create<ScrewSilver>(Vec(15, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x-30, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(15, 365)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x-30, 365)));

    addInput(Port::create<PJ301MPort>(Vec(22.5, 52), Port::INPUT, module, Erwin::TRANSPOSE_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(76, 52), Port::INPUT, module, Erwin::SEMI_INPUT));

    for(int i=0;i<4;i++) {
	addOutput(Port::create<PJ301MPort>(Vec(76, 247 + i*28), Port::OUTPUT, module, Erwin::OUT_OUTPUT + i));
	addInput(Port::create<PJ301MPort>(Vec(76, 112 +i*28), Port::INPUT, module, Erwin::IN_INPUT + i));

	addChild(ModuleLightWidget::create<SmallLight<GreenLight>>(Vec(74, 108 + i*28), module, Erwin::CHANNEL_MONITOR_LIGHT+i));
    }

    addParam(ParamWidget::create<Trimpot>(Vec(16, 95), module, Erwin::CHANNEL_TRANSPOSE_PARAM, -4, 4, 0));
    addParam(ParamWidget::create<Trimpot>(Vec(38, 95), module, Erwin::CHANNEL_TRANSPOSE_PARAM + 1, -4, 4, 0));
    addParam(ParamWidget::create<Trimpot>(Vec(16, 120), module, Erwin::CHANNEL_TRANSPOSE_PARAM + 2, -4, 4, 0));
    addParam(ParamWidget::create<Trimpot>(Vec(38, 120), module, Erwin::CHANNEL_TRANSPOSE_PARAM + 3, -4, 4, 0));

    addParam(ParamWidget::create<LEDButton>(Vec(12, 147), module, Erwin::CHANNEL_MONITOR_PARAM, 0, 1, 0));

    addParam(ParamWidget::create<RoundBlackSnapKnob>(Vec(74, 17), module, Erwin::SELECT_PARAM, 0, 15, 0));
    addInput(Port::create<PJ301MPort>(Vec(22.5, 17), Port::INPUT, module, Erwin::SELECT_INPUT));

    //Note buttons
    int white=0;
    int black = 0;
    for(int i=0; i<12; i++) {
	if (i == 1 || i == 3 || i == 6 || i == 8 || i == 10 ) {
	    addParam(ParamWidget::create<LEDBezel>(Vec(10,321.5 - black*30), module, Erwin::NOTE_PARAM + i, 0.0, 1.0, 0.0));
	    addChild(ModuleLightWidget::create<BigLight<GreenRedLight>>(Vec(12.5, 324 - black*30), module, Erwin::NOTE_LIGHT+2*i));
	    black++;
	}
	else {
	    if(i == 4)
		black++;
	    addParam(ParamWidget::create<LEDBezel>(Vec(35,336.5 - white*30), module, Erwin::NOTE_PARAM + i, 0.0, 1.0, 0.0));
	    addChild(ModuleLightWidget::create<BigLight<GreenRedLight>>(Vec(37.5, 339 - white*30), module, Erwin::NOTE_LIGHT+2*i));
	    white++;
	}
    }
}

struct ErwinMenuItem : MenuItem {
    Erwin *module;
    int mode_;
    void onAction(EventAction &e) override {
	module->mode = mode_;
    }
    void step() override {
	rightText = (module->mode == mode_) ? "✔" : "";
	MenuItem::step();
    }
};

/* Export scales */
struct ErwinSaveItem : MenuItem {
    Erwin *module;

    void onAction(EventAction &e) override {
	json_t* rootJ = module->toJson();
	if(rootJ) {
	    char* path = osdialog_file(OSDIALOG_SAVE, NULL, "Erwin.json", NULL);
	    if(path) {
		if (json_dump_file(rootJ, path, 0))
		    debug("Error: Can not export Erwin Scale file");
	    }
	    free(path);
	}
    }
};

/* Import scales */
struct ErwinLoadItem : MenuItem {
    Erwin *module;
    json_error_t error;

    void onAction(EventAction &e) override {
	char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
	if(path) {
	    json_t* rootJ = json_load_file(path, 0, &error);
	    if(rootJ) {
		//Check this here to not break compatibility with old (single scale) saves
		json_t *gatesJ = json_object_get(rootJ, "notes");
		if(!gatesJ || json_array_size(gatesJ) != 12 * NUM_SCALES) {
		    osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, "Erwin: Invalid Input file");
		    return;
		}
		module->fromJson(rootJ);
	    }
	    else {
		osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, "Can't load file. See Logfile for details");
		debug("Error: Can't import file %s", path);
		debug("Text: %s", error.text);
		debug("Source: %s", error.source);
	    }
	    free(path);
	}
    }
};

Menu *ErwinWidget::createContextMenu() {
    Menu *menu = ModuleWidget::createContextMenu();

    Erwin *erwin = dynamic_cast<Erwin*>(module);
    assert(erwin);

    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mode"));
    menu->addChild(construct<ErwinMenuItem>(&ErwinMenuItem::text, "Down", &ErwinMenuItem::module, erwin,  &ErwinMenuItem::mode_, Erwin::QModes::DOWN));
    menu->addChild(construct<ErwinMenuItem>(&ErwinMenuItem::text, "Up", &ErwinMenuItem::module, erwin,  &ErwinMenuItem::mode_, Erwin::QModes::UP));
    menu->addChild(construct<ErwinMenuItem>(&ErwinMenuItem::text, "Nearest", &ErwinMenuItem::module, erwin,  &ErwinMenuItem::mode_, Erwin::QModes::NEAREST));

    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Import/Export"));
    menu->addChild(construct<ErwinLoadItem>(&ErwinLoadItem::text, "Load Scales", &ErwinLoadItem::module, erwin));
    menu->addChild(construct<ErwinSaveItem>(&ErwinSaveItem::text, "Save Scales", &ErwinSaveItem::module, erwin));

    return menu;
}

Model *modelErwin = Model::create<Erwin, ErwinWidget>("Aepelzens Modules", "Erwin", "Erwin", UTILITY_TAG);

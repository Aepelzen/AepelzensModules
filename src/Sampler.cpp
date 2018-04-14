#include "aepelzen.hpp"
#include "AeFilter.hpp"
#include <vector>
#include <string>
#include <dirent.h>
#include <sys/types.h>
#include <algorithm>
#include <cmath>

#include <sndfile.h>

#include "dsp/frame.hpp"
#include "dsp/digital.hpp"
#include "dsp/samplerate.hpp"
#include "osdialog.h"

#ifdef ARCH_WIN
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

struct SampleInfo {
    std::string path;
    //file properties
    int channels, frames, rate;

    //resampled buffer
    Frame<2> *buffer = NULL;
    //bufferLength in frames
    int bufferLength = 0;
    //sample start in frames
    int start = 0;
    int end = 0;
    float gain = 1.0f;
};

struct AeSampler : Module {
    enum ParamIds {
	PITCH_PARAM,
	FILTER_PARAM,
	FILTER_Q_PARAM,
	SELECT_PARAM,
	GAIN_PARAM,
	SPEED_ATT_PARAM,
	FILTER_ATT_PARAM,
	SELECT_ATT_PARAM,
	GAIN_ATT_PARAM,
	DEL_PARAM,
	REVERSE_PARAM,
	SAMPLE_START_PARAM,
	SAMPLE_END_PARAM,
	SAMPLE_GAIN_PARAM,
	NUM_PARAMS
    };
    enum InputIds {
	GATE_INPUT,
	SELECT_INPUT,
	GAIN_INPUT,
	FILTER_INPUT,
	SPEED_INPUT,
	REVERSE_INPUT,
	NUM_INPUTS
    };
    enum OutputIds {
	L_OUTPUT,
	R_OUTPUT,
	NUM_OUTPUTS
    };
    enum LightIds {
	REVERSE_LIGHT,
	NUM_LIGHTS
    };

    SampleRateConverter<2> converter;

    bool gate = false;
    bool reverse = false;
    float phase = 0.0f;
    SchmittTrigger gateTrigger;
    SchmittTrigger RemoveTrigger;
    SchmittTrigger ReverseTrigger;
    SchmittTrigger ReverseInputTrigger;

    float startParam = 10.0f;
    float endParam = 10.0f;
    float gainParam = 0.0f;

    AeFilterFrame<2> filter;

    const float LP_MAX_FREQ = 16000.0f;
    const float LP_MIN_FREQ = 30.0f;
    const float HP_MAX_FREQ = 16000.0f;
    const float HP_MIN_FREQ = 50.0f;

    std::string lastPath = "";
    //file properties
    bool fileLoaded = false;
    std::vector<SampleInfo> samples;
    SampleInfo *activeSample = NULL;
    unsigned int index = 0;

    void step() override;
    void loadFile(const char* path);
    void loadDir(const char* path);
    Frame<2> interpolateFrame(Frame<2> *buf, float phase);

    AeSampler() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {};

    void removeSample() {
	if(activeSample) {
	    free(activeSample->buffer);
	    samples.erase(samples.begin() + index);

	    if(samples.empty()) {
		activeSample = NULL;
		fileLoaded = false;
		index = 0;
	    }
	    else if(index >= samples.size()) {
		index = samples.size() - 1;
		activeSample = &samples.back();
	    }
	}
    }

    void freeSamples() {
	debug("freeSamples()");
	//reset these first so the display widget doesn't freak out
	fileLoaded = false;
	activeSample = NULL;
	index = 0;

	for(std::vector<SampleInfo>::const_iterator iter = samples.begin(); iter != samples.end();++iter) {
	    if((*iter).buffer) free((*iter).buffer);
	}
	samples.clear();
    }

    void onSampleRateChange() override  {
	//get filenames for all loaded files
	std::vector<std::string> filenames;
	for(std::vector<SampleInfo>::const_iterator iter = samples.begin(); iter != samples.end();++iter) {
	    if(!iter->path.empty()) filenames.push_back(iter->path);
	    //debug("found sample %s", iter->path.c_str());
	}
	freeSamples();
	//load them
	for(std::vector<std::string>::const_iterator iter = filenames.begin(); iter != filenames.end();++iter) {
	    loadFile((*iter).c_str());
	}
	filenames.clear();
    }

    ~AeSampler() {
	freeSamples();
    }

    void reset() override {
	freeSamples();
    }

    json_t* toJson() override {
	json_t *rootJ = json_object();
	json_t *samplesJ = json_array();

	for(std::vector<SampleInfo>::const_iterator iter = samples.begin(); iter != samples.end();++iter) {
	    if(!iter->path.empty()) {
		//sampleInfo entry (path, start, end, gain)
		json_t *entryJ = json_array();

		json_t *fileJ = json_string((*iter).path.c_str());
		json_array_append_new(entryJ, fileJ);
		json_t *startJ = json_integer(iter->start);
		json_array_append_new(entryJ, startJ);
		json_t *endJ = json_integer(iter->end);
		json_array_append_new(entryJ, endJ);
		json_t *gainJ = json_real(iter->gain);
		json_array_append_new(entryJ, gainJ);

		json_array_append_new(samplesJ, entryJ);
	    }
	}
	json_object_set_new(rootJ, "files", samplesJ);
	return rootJ;
    };

    void fromJson(json_t *rootJ) override {
	json_t *samplesJ = json_object_get(rootJ, "files");
	int size = json_array_size(samplesJ);
	for(int i=0;i<size;i++) {
	    json_t *entryJ = json_array_get(samplesJ, i);
	    if(entryJ) {
		json_t *fileJ = json_array_get(entryJ,0);
		//debug("found sample: %s", json_string_value(fileJ));
		if (fileJ) loadFile(json_string_value(fileJ));
		json_t* startJ = json_array_get(entryJ,1);
		if(startJ) samples[i].start = json_integer_value(startJ);
		json_t* endJ = json_array_get(entryJ,2);
		if(endJ) samples[i].end = json_integer_value(endJ);
		json_t* gainJ = json_array_get(entryJ,3);
		if(gainJ) samples[i].gain = json_number_value(gainJ);
	    }
	}
    };
};

bool compareSampleInfos (SampleInfo i,SampleInfo j) {
    return (i.path < j.path);
}

inline Frame<2> AeSampler::interpolateFrame(Frame<2> *buf, float phase) {
    float i = 0;
    float frac = modf(phase,&i);
    int index = clamp((int)i,0,activeSample->bufferLength-2);

    Frame<2> out;
    out.samples[0] = buf[index].samples[0] + frac * (buf[index+1].samples[0] - buf[index].samples[0]);
    out.samples[1] = buf[index].samples[1] + frac * (buf[index+1].samples[1] - buf[index].samples[1]);
    return out;
}

void AeSampler::loadFile(const char* path) {
    SampleInfo si;
    SF_INFO info;
    SF_INFO* infop = &info;

    info.format = 0;
    SNDFILE* file = sf_open(path, SFM_READ, infop);

    if(!file) {
	rack::info("Error while trying to read file %s", path);
	rack::info(sf_strerror(file));
	return;
    }

    si.rate = infop->samplerate;
    si.channels = infop->channels;
    si.frames = infop->frames;
    debug("Open file: %s", path);
    debug("channels: %i, rate: %i, frames:%i", si.channels, si.rate, si.frames);

    float* filebuffer = (float*)malloc(si.frames * si.channels * sizeof(float));
    //read file (length is in frames)
    int length = sf_readf_float(file, filebuffer, si.frames);
    //debug("read %i frames",  length);

    if(length != si.frames) {
	int error = sf_error(file);
	rack::info("Error while trying to read file %s", path);
	rack::info("Error: %i. %s", error, sf_error_number(error));
	free(filebuffer);
	sf_close(file);
	return;
    }

    sf_close(file);

    //convert to frame
    //int inputFrames = si.frames/si.channels;
    int inputFrames = si.frames;
    Frame<2> *inbuffer = (Frame<2>*)malloc(inputFrames * sizeof(Frame<2>));
    if(si.channels == 1) {
	for(int i=0;i<si.frames;i++) {
	    inbuffer[i].samples[0] = filebuffer[i];
	    inbuffer[i].samples[1] = filebuffer[i];
	}
    }
    else if(si.channels > 1) {
	for(int i=0;i<inputFrames;i++) {
	    inbuffer[i].samples[0] = filebuffer[i*si.channels];
	    inbuffer[i].samples[1] = filebuffer[i*si.channels+1];
	}
    }
    free(filebuffer);

    //convert to global samplerate
    if(si.rate == engineGetSampleRate()) {
	si.buffer = inbuffer;
	si.bufferLength = inputFrames;
    }
    else {
	float ratio = engineGetSampleRate()/si.rate;
	si.bufferLength = ceil(ratio * inputFrames);
	si.buffer = (Frame<2>*)malloc(si.bufferLength  * sizeof(Frame<2>));

	converter.refreshState();
	converter.setChannels(2);
	converter.setRates(si.rate, engineGetSampleRate());
	//debug("call converter with %i input and %i output frames", inputFrames, si.bufferLength);
	converter.process(inbuffer, &inputFrames, si.buffer, &(si.bufferLength));
	//debug("done. Got %i samples",si.bufferLength);
	//only free this when it was converted
	free(inbuffer);
    }

    si.path = path;
    si.end = si.bufferLength;
    samples.push_back(si);
    fileLoaded = true;
};

#ifndef ARCH_WIN
void AeSampler::loadDir(const char* path) {
    struct dirent *entry;
    DIR *dir = opendir(path);
    if (dir == NULL) {
	return;
    }

    //clear old samples
    freeSamples();

    while ((entry = readdir(dir)) != NULL) {
	if(entry->d_type == DT_REG) {
	    std::string p = path + std::string("/") + std::string(entry->d_name);
	    loadFile(p.c_str());
	}
    }
    closedir(dir);

    //readdir returns files unordered (order by filenames)
    if (samples.size() > 1) sort(samples.begin(), samples.end(), compareSampleInfos);
}
#endif

struct TinyEncoder : Trimpot {
    TinyEncoder() {
	smooth = false;
    }
};

void AeSampler::step() {

    //Process Triggers
    if(ReverseTrigger.process(params[REVERSE_PARAM].value) || ReverseInputTrigger.process(inputs[REVERSE_INPUT].value)) {
	reverse = !reverse;
    }

    if(RemoveTrigger.process(params[DEL_PARAM].value)) {
	removeSample();
    }

    float speed = clamp(params[PITCH_PARAM].value + inputs[SPEED_INPUT].value * params[SPEED_ATT_PARAM].value * 3.0f/5.0f, -3.0f, 3.0f);
    speed = pow(2,speed);
    //linear fm
    //float speed = params[PITCH_PARAM].value;
    //speed = clamp(speed * (1 + inputs[SPEED_INPUT].value  /5.0f * params[SPEED_ATT_PARAM].value), 0.05f,3.0f);

    if(reverse) speed*=(-1.0f);

    float gain = clamp(params[GAIN_PARAM].value + inputs[GAIN_INPUT].value * params[GAIN_ATT_PARAM].value / 5.0, 0.0f, 1.0f);
    //gain = exp(gain/20);
    //gain = pow(10, gain/20);

    float newStartParam = params[SAMPLE_START_PARAM].value;
    if(newStartParam != startParam) {
	float delta = (newStartParam - startParam);
	startParam = newStartParam;
	//filter out jumps during initialisation (set smooth = false !!!)
	if(activeSample && abs(delta) <= 0.3) {
	    delta*=(activeSample->bufferLength * 0.1f);
	    activeSample->start += delta;
	    activeSample->start = clamp(activeSample->start,0, activeSample->end);
	}
    }

    float newEndParam = params[SAMPLE_END_PARAM].value;
    if(newEndParam != endParam) {
	float delta = (newEndParam - endParam);
	endParam = newEndParam;
	//filter out jumps during initialisation (set smooth = false !!!)
	if(activeSample && abs(delta) <= 0.3) {
	    delta*=(activeSample->bufferLength * 0.1f);
	    activeSample->end += delta;
	    activeSample->end = clamp(activeSample->end, activeSample->start, activeSample->bufferLength);
	}
    }

    float newGainParam = params[SAMPLE_GAIN_PARAM].value;
    if(newGainParam != gainParam) {
	float delta = (newGainParam - gainParam);
	gainParam = newGainParam;
	//filter out jumps during initialisation (set smooth = false !!!)
	if(activeSample && abs(delta) <= 0.3) {
	    delta*=0.5f;
	    activeSample->gain += delta;
	    activeSample->gain = clamp(activeSample->gain, 0.0f, 2.0f);
	}
    }

    index = round(clamp(params[SELECT_PARAM].value + params[SELECT_ATT_PARAM].value * inputs[SELECT_INPUT].value / 5.0f, 0.0f, 1.0f) * (samples.size() - 1));
    if(samples.empty()) {
	activeSample = NULL;
	index = 0;
    }
    else
	activeSample = &samples[index];

    if(!fileLoaded || !activeSample) {
	outputs[L_OUTPUT].value = 0.0f;
	outputs[R_OUTPUT].value = 0.0f;
	return;
    }

    if(gateTrigger.process(inputs[GATE_INPUT].value)) {
	phase = reverse ? activeSample->end - 1 : activeSample->start;
	gate = true;
    }

    Frame<2> out;
    if ((phase < activeSample->end) && (phase >= activeSample->start) && gate){
	phase+=speed;
	out = interpolateFrame(activeSample->buffer,phase);
    }
    else{
	gate = false;
	out.samples[0] = 0.0f;
	out.samples[1] = 0.0f;
    }

    float filterParam = clamp(params[FILTER_PARAM].value + inputs[FILTER_INPUT].value * params[FILTER_ATT_PARAM].value / 5.0f, 0.0f, 1.0f) * 2.0f;
    float q = params[FILTER_Q_PARAM].value;

    if(filterParam != 1.0f) {
	float freq;
	if(filterParam > 1.0f) {
	    freq = HP_MIN_FREQ * powf(HP_MAX_FREQ / HP_MIN_FREQ, filterParam - 1.0f);
	    filter.setCutoff(freq, q, AeFilterType::AeHIGHPASS);
	}
	else {
	    freq = LP_MIN_FREQ * powf(LP_MAX_FREQ / LP_MIN_FREQ, filterParam);
	    filter.setCutoff(freq, q, AeFilterType::AeLOWPASS);
	}
	//apply filter
	out = filter.process(out);
    }

    outputs[L_OUTPUT].value = out.samples[0] * 5.0f * gain * activeSample->gain;
    outputs[R_OUTPUT].value = out.samples[1] * 5.0f * gain * activeSample->gain;
    lights[REVERSE_LIGHT].value = reverse ? 1.0f : 0.0f;
}


struct SampleDisplay : TransparentWidget {
    AeSampler *module;
    std::shared_ptr<Font> font;
    std::string displayParams;

    SampleDisplay() {
	//font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	font = Font::load(assetGlobal("res/fonts/DejaVuSans.ttf"));
    }

    void draw(NVGcontext *vg) override {
	//info
	nvgFontSize(vg, 12);
	nvgFontFaceId(vg, font->handle);
	nvgStrokeWidth(vg, 1);
	nvgTextAlign(vg, NVG_ALIGN_RIGHT);
	nvgFillColor(vg, nvgRGBA(0xdc, 0x75, 0x2f, 255));

	std::string sizeText = std::to_string(module->index + 1) + "/" + std::to_string(module->samples.size());
	nvgTextBox(vg, 90, 10, 40, sizeText.c_str(),NULL);

	// Draw ref line
	nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
	nvgStrokeWidth(vg, 1);
	{
	    nvgBeginPath(vg);
	    nvgMoveTo(vg, 0, 35);
	    nvgLineTo(vg, 130, 35);
	    nvgClosePath(vg);
	}
	nvgStroke(vg);

	if(module->activeSample) {
	    //Warning: activeSample can still be NULL in the following parts (maybe a multithreading thing?)
	    SampleInfo* si = module->activeSample;

	    nvgTextAlign(vg, NVG_ALIGN_LEFT);
	    char gainText[16];
	    float gain = (si) ? si->gain : 0.00f;
	    snprintf(gainText, 16, "Gain: %2.1f dB", 20 * log10f(gain));
	    nvgTextBox(vg, 0, 10, 80, gainText, NULL);

	    // Draw waveform
	    nvgStrokeColor(vg, nvgRGBA(42, 161, 174, 255));
	    nvgSave(vg);
	    //upper left corner and size (relative to displayBox)
	    Rect b = Rect(Vec(0, 10), Vec(130, 50));
	    nvgScissor(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
	    nvgBeginPath(vg);
	    //apparently this can be NULL
	    for (int i = 0; si && i < si->bufferLength; i++) {
		float x, y;
		x = (si) ? (float)i / (si->bufferLength - 1.0f) : 0.0f;;
		y = (si) ? si->buffer[i].samples[0] * gain /2.0f + 0.5f  : 0.0f;
		Vec p;
		p.x = b.pos.x + b.size.x * x;
		p.y = b.pos.y + b.size.y * (1.0f - y);
		if (i == 0)
		    nvgMoveTo(vg, p.x, p.y);
		else
		    nvgLineTo(vg, p.x, p.y);

	    }
	    nvgLineCap(vg, NVG_ROUND);
	    nvgMiterLimit(vg, 2.0);
	    nvgStrokeWidth(vg, 1);
	    //nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
	    nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
	    nvgStroke(vg);
	    nvgResetScissor(vg);
	    nvgRestore(vg);

	    // Draw start line
	    nvgStrokeColor(vg, nvgRGBA(0xbc, 0x63, 0xc5, 255));
	    nvgBeginPath(vg);
	    nvgStrokeWidth(vg, 1);
	    int x = (si) ? clamp((int)(130.0f * si->start/si->bufferLength), 1, 130) : 0;
	    nvgMoveTo(vg,x, 10);
	    nvgLineTo(vg,x, 60);
	    nvgClosePath(vg);
	    nvgStroke(vg);

	    // Draw end line
	    nvgStrokeColor(vg, nvgRGBA(0xbc, 0x63, 0xc5, 255));
	    nvgBeginPath(vg);
	    nvgStrokeWidth(vg, 1);
	    x = (si) ? clamp((int)(130.0f * si->end/si->bufferLength), 1, 129) : 0;
	    nvgMoveTo(vg,x, 10);
	    nvgLineTo(vg,x, 60);
	    nvgClosePath(vg);
	    nvgStroke(vg);

	    // Draw play line
	    nvgStrokeColor(vg, nvgRGBA(168, 15, 15, 255));
	    nvgBeginPath(vg);
	    nvgStrokeWidth(vg, 1);
	    x = (si) ? clamp((int)(module->phase/si->bufferLength * 130), 1, 129) : 0;
	    nvgMoveTo(vg,x, 10);
	    nvgLineTo(vg,x, 60);
	    nvgClosePath(vg);
	    nvgStroke(vg);
	}
    }
};

struct AeLoadDirButton : LEDButton {
    AeSampler *module;

#ifndef ARCH_WIN
    void onAction(EventAction &e) override {
	char* path = osdialog_file(OSDIALOG_OPEN_DIR, module->lastPath.c_str(), NULL, NULL);
	if(path) {
	    module->loadDir(path);
	    module->lastPath = path;
	    free(path);
	}
    }
#endif
};

struct AeLoadButton : LEDButton {
    AeSampler *module;

    void onAction(EventAction &e) override {
	char* path = osdialog_file(OSDIALOG_OPEN, module->lastPath.c_str(), NULL, NULL);
	if(path) {
	    module->loadFile(path);
	    std::string lp  = path;
	    module->lastPath = lp.substr(0,lp.find_last_of(PATH_SEP));
	    // int lastIndex = strlen(path) - 1;
	    // while(path[lastIndex] != PATH_SEP) lastIndex--;
	    // if(lastIndex < 128) {
	    //	strncpy(module->lastPath,path,lastIndex);
	    // }
	    free(path);
	}
    }
};

struct AeSamplerWidget : ModuleWidget {
    AeSamplerWidget(AeSampler *module) : ModuleWidget(module) {
	setPanel(SVG::load(assetPlugin(plugin, "res/Sampler.svg")));

	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	{
	    SampleDisplay *display = new SampleDisplay();
	    display->module = module;
	    display->box.pos = Vec(10, 25);
	    display->box.size = Vec(130, 60);
	    addChild(display);

	    //LOAD Buttons
	    AeLoadDirButton *ldb = new AeLoadDirButton();
	    ldb->module = module;
	    ldb->box.pos = Vec(10, 150);
	    addChild(ldb);

	    AeLoadButton* loadButton = new AeLoadButton();
	    loadButton->module = module;
	    loadButton->box.pos = Vec(10, 126);
	    addChild(loadButton);
	}

	addParam(ParamWidget::create<LEDButton>(Vec(122, 150), module, AeSampler::DEL_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<LEDButton>(Vec(122, 126), module, AeSampler::REVERSE_PARAM, 0.0f, 1.0f, 0.0f));
	addChild(ModuleLightWidget::create<SmallLight<RedLight>>(Vec(138, 120), module, AeSampler::REVERSE_LIGHT));

	addParam(ParamWidget::create<Davies1900hLargeBlackKnob>(Vec(48, 120), module, AeSampler::SELECT_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<RoundBlackKnob>(Vec(10, 205), module, AeSampler::GAIN_PARAM, 0.0f, 1.0f, 1.0f));
	addParam(ParamWidget::create<RoundBlackKnob>(Vec(58, 205), module, AeSampler::PITCH_PARAM, -2.0f, 2.0f, 0.0f));
	addParam(ParamWidget::create<RoundBlackKnob>(Vec(105, 205), module, AeSampler::FILTER_PARAM, 0.0f, 1.0f, 0.5f));

	addParam(ParamWidget::create<Trimpot>(Vec(125, 188), module, AeSampler::FILTER_Q_PARAM, 0.5f, 2.0f, 0.8f));

	addParam(ParamWidget::create<Trimpot>(Vec(10, 260), module, AeSampler::SELECT_ATT_PARAM, -1.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<Trimpot>(Vec(46, 260), module, AeSampler::GAIN_ATT_PARAM, -1.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<Trimpot>(Vec(82, 260), module, AeSampler::SPEED_ATT_PARAM, -1.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<Trimpot>(Vec(118, 260), module, AeSampler::FILTER_ATT_PARAM, -1.0f, 1.0f, 0.0f));

	addInput(Port::create<PJ301MPort>(Vec(6, 295), Port::INPUT, module, AeSampler::SELECT_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(42, 295), Port::INPUT, module, AeSampler::GAIN_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(78, 295), Port::INPUT, module, AeSampler::SPEED_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(114, 295), Port::INPUT, module, AeSampler::FILTER_INPUT));

	addInput(Port::create<PJ301MPort>(Vec(6, 340), Port::INPUT, module, AeSampler::GATE_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(42, 340), Port::INPUT, module, AeSampler::REVERSE_INPUT));
	addOutput(Port::create<PJ301MPort>(Vec(78, 340), Port::OUTPUT, module, AeSampler::L_OUTPUT));
	addOutput(Port::create<PJ301MPort>(Vec(114, 340), Port::OUTPUT, module, AeSampler::R_OUTPUT));

	//Encoders for per-sample parameters
	addParam(ParamWidget::create<TinyEncoder>(Vec(10, 88), module, AeSampler::SAMPLE_START_PARAM, -INFINITY, INFINITY, 0.0f));
	addParam(ParamWidget::create<TinyEncoder>(Vec(70, 88), module, AeSampler::SAMPLE_END_PARAM, -INFINITY, INFINITY, 0.0f));
	addParam(ParamWidget::create<TinyEncoder>(Vec(122, 88), module, AeSampler::SAMPLE_GAIN_PARAM, -INFINITY, INFINITY, 0.0f));
    }
};

Model *modelAeSampler = Model::create<AeSampler, AeSamplerWidget>("Aepelzens Modules", "Sampler", "DrumSampler", SAMPLER_TAG);

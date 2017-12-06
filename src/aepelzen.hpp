#include "rack.hpp"

using namespace rack;

extern Plugin *plugin;

////////////////////
// module widgets
////////////////////

struct TriSEQ3Widget : ModuleWidget {
	TriSEQ3Widget();
	json_t *toJsonData();
	void fromJsonData(json_t *root);
};

struct GateSEQ8Widget : ModuleWidget {
	GateSEQ8Widget();
	json_t *toJsonData();
	void fromJsonData(json_t *root);
};

struct QuadSeqWidget : ModuleWidget {
	QuadSeqWidget();
};

struct dTimeWidget : ModuleWidget {
	dTimeWidget();
};

struct Knob29 : RoundKnob {
	Knob29() {
		setSVG(SVG::load(assetPlugin(plugin, "res/knob_29px.svg")));
		box.size = Vec(29, 29);
	}
};

struct BurstWidget : ModuleWidget
{
	SVGPanel *panel;
	BurstWidget();
  //void step() override;
};

struct QuantumWidget : ModuleWidget {
	QuantumWidget();
	json_t *toJsonData() ;
	void fromJsonData(json_t *root) ;
	Menu *createContextMenu() override;
};

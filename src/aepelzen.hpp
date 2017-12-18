#include "rack.hpp"

using namespace rack;

extern Plugin *plugin;

////////////////////
// module widgets
////////////////////

struct GateSeqWidget : ModuleWidget {
	GateSeqWidget();
	ParamWidget *lengthParams[8];
	ParamWidget *probParams[8];
	void updateValues();
};

struct QuadSeqWidget : ModuleWidget {
	QuadSeqWidget();
};

// struct dTimeWidget : ModuleWidget {
// 	dTimeWidget();
// };

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

struct FolderWidget : ModuleWidget
{
	SVGPanel *panel;
	FolderWidget();
	Menu *createContextMenu() override;
};

struct WalkerWidget : ModuleWidget
{
	SVGPanel *panel;
	WalkerWidget();
};

struct ErwinWidget : ModuleWidget {
	ErwinWidget();
};

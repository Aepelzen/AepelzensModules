#include "rack.hpp"

using namespace rack;

extern Plugin *plugin;

/* the original modulo does not deal with negative numbers correctly
   For example -1%12 should be 11, but it is -1*/
inline int modN(int k, int n) {
    return ((k %= n) < 0) ? k+n : k;
}

/* modified version of ceil that works with negative values (example: -2.3 becomes -3) */
inline int ceilN(float x) {
    return (x < 0) ? (int)floor(x) : (int)ceil(x);
}

struct Knob29 : RoundKnob {
	Knob29() {
		setSVG(SVG::load(assetPlugin(plugin, "res/knob_29px.svg")));
		box.size = Vec(29, 29);
	}
};

struct CKSSThreeH : SVGSwitch, ToggleSwitch {
	CKSSThreeH() {
	    addFrame(SVG::load(assetPlugin(plugin, "res/ComponentLibrary/CKSSThreeH_0.svg")));
	    addFrame(SVG::load(assetPlugin(plugin, "res/ComponentLibrary/CKSSThreeH_1.svg")));
	    addFrame(SVG::load(assetPlugin(plugin, "res/ComponentLibrary/CKSSThreeH_2.svg")));
	}
};

struct SnapTrimpot : Trimpot {
    SnapTrimpot() {
	snap = true;
	smooth = false;
    }
};

template <typename BASE>
struct BigLight : BASE {
	BigLight() {
	    this->box.size = Vec(17, 17);
	}
};

extern Model *modelQuadSeq;
extern Model *modelGateSeq;
extern Model *modelDice;
extern Model *modelBurst;
extern Model *modelFolder;
extern Model *modelWalker;
extern Model *modelErwin;
extern Model *modelWerner;
extern Model *modelAeSampler;

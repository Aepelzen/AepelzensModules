#include "aepelzen.hpp"
#include <math.h>

Plugin *plugin;

void init(rack::Plugin *p) {
	plugin = p;
	plugin->slug = "Aepelzens Modules";
#ifdef VERSION
	p->version = TOSTRING(VERSION);
#endif
	//p->website = "https://github.com/dekstop/vcvrackplugins_dekstop";

	p->addModel(createModel<QuadSeqWidget>("Aepelzens Modules", "QuadSeq", "Quad Sequencer", SEQUENCER_TAG));
	p->addModel(createModel<GateSEQ8Widget>("Aepelzens Modules", "GateSEQ8", "Gate SEQ-8", SEQUENCER_TAG));
	//p->addModel(createModel<dTimeWidget>("Aepelzens Modules", "dTime", "dTime Sequencer", SEQUENCER_TAG));
	p->addModel(createModel<BurstWidget>("Aepelzens Modules", "burst", "Burst Generator", SEQUENCER_TAG));
	//p->addModel(createModel<QuantumWidget>("Aepelzens Modules", "Quantum", "Quantum4", UTILITY_TAG));
}

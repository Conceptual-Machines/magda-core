// AUTO-GENERATED from magda_drive.dsp — do not edit by hand.
// Regenerate via scripts/regen-faust.sh after editing the .dsp source.
//
// Requires the vendored Faust runtime headers from third_party/faust-runtime/
// (faust/dsp/dsp.h, faust/gui/UI.h, faust/gui/meta.h) to be included first.

/* ------------------------------------------------------------
name: "MagdaDrive"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn MagdaDriveDsp -scn ::dsp -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __MagdaDriveDsp_H__
#define  __MagdaDriveDsp_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

/* link with : "" */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS MagdaDriveDsp
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

static float MagdaDriveDsp_faustpower2_f(float value) {
	return value * value;
}

class MagdaDriveDsp : public ::dsp {
	
 private:
	
	FAUSTFLOAT fHslider0;
	int fSampleRate;
	float fConst0;
	FAUSTFLOAT fHslider1;
	FAUSTFLOAT fHslider2;
	float fRec0[3];
	float fRec1[3];
	
 public:
	MagdaDriveDsp() {
	}
	
	MagdaDriveDsp(const MagdaDriveDsp&) = default;
	
	virtual ~MagdaDriveDsp() = default;
	
	MagdaDriveDsp& operator=(const MagdaDriveDsp&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.22.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn MagdaDriveDsp -scn ::dsp -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("description", "Stage 1 Faust POC effect: drive + lowpass + gain, stereo.");
		m->declare("filename", "magda_drive.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "MagdaDrive");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
	}

	virtual int getNumInputs() {
		return 2;
	}
	virtual int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = 3.1415927f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(2e+04f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.0f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 3; l0 = l0 + 1) {
			fRec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 3; l1 = l1 + 1) {
			fRec1[l1] = 0.0f;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual MagdaDriveDsp* clone() {
		return new MagdaDriveDsp(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("MagdaDrive");
		ui_interface->declare(&fHslider1, "unit", "Hz");
		ui_interface->addHorizontalSlider("Cutoff", &fHslider1, FAUSTFLOAT(2e+04f), FAUSTFLOAT(2e+02f), FAUSTFLOAT(2e+04f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fHslider2, "unit", "dB");
		ui_interface->addHorizontalSlider("Drive", &fHslider2, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(24.0f), FAUSTFLOAT(0.1f));
		ui_interface->declare(&fHslider0, "unit", "dB");
		ui_interface->addHorizontalSlider("Gain", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-24.0f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = std::tan(fConst0 * static_cast<float>(fHslider1));
		float fSlow1 = 1.0f / fSlow0;
		float fSlow2 = (fSlow1 + 1.4142135f) / fSlow0 + 1.0f;
		float fSlow3 = std::pow(1e+01f, 0.05f * static_cast<float>(fHslider0)) / fSlow2;
		float fSlow4 = std::pow(1e+01f, 0.05f * static_cast<float>(fHslider2));
		float fSlow5 = 1.0f / fSlow2;
		float fSlow6 = (fSlow1 + -1.4142135f) / fSlow0 + 1.0f;
		float fSlow7 = 2.0f * (1.0f - 1.0f / MagdaDriveDsp_faustpower2_f(fSlow0));
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			fRec0[0] = tanhf(fSlow4 * static_cast<float>(input0[i0])) - fSlow5 * (fSlow6 * fRec0[2] + fSlow7 * fRec0[1]);
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow3 * (fRec0[2] + fRec0[0] + 2.0f * fRec0[1]));
			fRec1[0] = tanhf(fSlow4 * static_cast<float>(input1[i0])) - fSlow5 * (fSlow6 * fRec1[2] + fSlow7 * fRec1[1]);
			output1[i0] = static_cast<FAUSTFLOAT>(fSlow3 * (fRec1[2] + fRec1[0] + 2.0f * fRec1[1]));
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
		}
	}

};

#endif

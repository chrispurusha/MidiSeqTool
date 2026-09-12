// MidiSeqTool's offline check (design §8.3): a minimal VST3 host that plays the built plug-in a
// 4-bar drum part for five passes and checks what comes out.
//
//   tools/do-msqtest && tools/msqtest [build/MidiSeqTool.vst3]
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

class EventList : public IEventList {
public:
    std::vector<Event> events;

    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IEventList::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return (int32)events.size(); }
    tresult PLUGIN_API getEvent(int32 index, Event & e) override {
        if ((index < 0) || (index >= (int32)events.size())) {
            return kInvalidArgument;
        }
        e = events[(size_t)index];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event & e) override {
        events.push_back(e);
        return kResultOk;
    }
};

class Host : public IHostApplication {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IHostApplication::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getName(String128 name) override {
        const char * n = "msqtest";
        for (int i = 0; i < 8; i++) {
            name[i] = (char16)n[i];
        }
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void ** obj) override {
        (void)cid;
        (void)iid;
        *obj = nullptr;
        return kNotImplemented;
    }
};

struct Played {
    double  ppq;
    bool    on;
    uint8_t note;
    uint8_t velocity;
};

static const double kRate   = 48000.0;
static const int    kBlock  = 512;
static const double kTempo  = 120.0;
static const double kPass   = 16.0;    // four bars of 4/4, in quarter notes
static const int    kPasses = 5;

// The part: kick on every beat, snare on 2 and 4, closed hat on every eighth, each 0.1 of a beat long.
static std::vector<Played> part(void) {
    std::vector<Played> notes;

    for (int pass = 0; pass < kPasses; pass++) {
        for (int eighth = 0; eighth < 32; eighth++) {
            double at = (pass * kPass) + (eighth * 0.5);

            notes.push_back({ at, true, 42, 80 });
            notes.push_back({ at + 0.1, false, 42, 0 });

            if ((eighth % 2) == 0) {
                notes.push_back({ at, true, 36, 110 });
                notes.push_back({ at + 0.1, false, 36, 0 });
            }
            if ((eighth % 4) == 2) {
                notes.push_back({ at, true, 38, 100 });
                notes.push_back({ at + 0.1, false, 38, 0 });
            }
        }
    }
    // IN TIME ORDER - the host below feeds them in list order.
    std::stable_sort(notes.begin(), notes.end(), [](const Played & a, const Played & b) { return a.ppq < b.ppq; });
    return notes;
}

int main(int argc, char ** argv) {
    const char * bundle = (argc > 1) ? argv[1] : "build/MidiSeqTool.vst3";
    char         binary[1024];

    snprintf(binary, sizeof(binary), "%s/Contents/MacOS/MidiSeqTool", bundle);
    void * lib = dlopen(binary, RTLD_NOW | RTLD_LOCAL);

    if (lib == nullptr) {
        fprintf(stderr, "cannot load %s: %s\n", binary, dlerror());
        return 1;
    }
    typedef bool (* BundleEntry)(CFBundleRef);
    typedef IPluginFactory * (* GetFactory)(void);
    BundleEntry entry = (BundleEntry)dlsym(lib, "bundleEntry");

    if (entry != nullptr) {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)bundle, (CFIndex)strlen(bundle), true);
        entry(CFBundleCreate(nullptr, url));
    }
    IPluginFactory * factory = ((GetFactory)dlsym(lib, "GetPluginFactory"))();
    IComponent *     component = nullptr;

    for (int32 i = 0; i < factory->countClasses(); i++) {
        PClassInfo info;

        factory->getClassInfo(i, &info);
        if (strcmp(info.category, kVstAudioEffectClass) == 0) {
            factory->createInstance(info.cid, IComponent::iid, (void **)&component);
            break;
        }
    }
    if (component == nullptr) {
        fprintf(stderr, "no processor class\n");
        return 1;
    }
    Host host;

    component->initialize(&host);
    IAudioProcessor * processor = nullptr;

    component->queryInterface(IAudioProcessor::iid, (void **)&processor);
    printf("buses: event in %d, event out %d, audio out %d\n", component->getBusCount(kEvent, kInput),
           component->getBusCount(kEvent, kOutput), component->getBusCount(kAudio, kOutput));
    ProcessSetup setup = { kRealtime, kSample32, kBlock, kRate };

    processor->setupProcessing(setup);
    component->activateBus(kEvent, kInput, 0, true);
    component->activateBus(kEvent, kOutput, 0, true);
    component->activateBus(kAudio, kOutput, 0, true);
    component->setActive(true);
    processor->setProcessing(true);

    std::vector<Played> input = part();
    std::vector<Played> output;
    float               left[kBlock], right[kBlock];
    float *             channels[2] = { left, right };
    double              perSample = (kTempo / 60.0) / kRate;
    size_t              next      = 0;
    int                 blocks    = (int)ceil((kPasses * kPass) / (perSample * kBlock)) + 1;

    for (int b = 0; b <= blocks; b++) {
        bool           playing = (b < blocks);    // the last block is a stop
        double         ppq     = b * kBlock * perSample;
        EventList      in, out;
        ProcessContext ctx = {};
        AudioBusBuffers audio = {};
        ProcessData    data;

        ctx.state              = (playing ? ProcessContext::kPlaying : 0) | ProcessContext::kTempoValid |
                                 ProcessContext::kProjectTimeMusicValid | ProcessContext::kBarPositionValid |
                                 ProcessContext::kTimeSigValid;
        ctx.sampleRate         = kRate;
        ctx.projectTimeSamples = (TSamples)b * kBlock;
        ctx.projectTimeMusic   = ppq;
        ctx.barPositionMusic   = floor(ppq / 4.0) * 4.0;
        ctx.tempo              = kTempo;
        ctx.timeSigNumerator   = 4;
        ctx.timeSigDenominator = 4;

        while (playing && (next < input.size()) && (input[next].ppq < (ppq + (kBlock * perSample)))) {
            Event e = {};

            e.sampleOffset = (int32)floor((input[next].ppq - ppq) / perSample);
            if (input[next].on) {
                e.type             = Event::kNoteOnEvent;
                e.noteOn.pitch     = input[next].note;
                e.noteOn.velocity  = input[next].velocity / 127.0f;
                e.noteOn.noteId    = -1;
            } else {
                e.type             = Event::kNoteOffEvent;
                e.noteOff.pitch    = input[next].note;
                e.noteOff.noteId   = -1;
            }
            in.events.push_back(e);
            next++;
        }
        audio.numChannels      = 2;
        audio.channelBuffers32 = channels;
        data.processMode       = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples        = kBlock;
        data.numInputs         = 0;
        data.numOutputs        = 1;
        data.outputs           = &audio;
        data.inputEvents       = &in;
        data.outputEvents      = &out;
        data.processContext    = &ctx;
        processor->process(data);

        for (const Event & e : out.events) {
            double at = ppq + (e.sampleOffset * perSample);

            if (e.type == Event::kNoteOnEvent) {
                output.push_back({ at, true, (uint8_t)e.noteOn.pitch, (uint8_t)lroundf(e.noteOn.velocity * 127.0f) });
            } else if (e.type == Event::kNoteOffEvent) {
                output.push_back({ at, false, (uint8_t)e.noteOff.pitch, 0 });
            }
        }
    }
    processor->setProcessing(false);
    component->setActive(false);

    // What came out, pass by pass.
    int inOnPerPass = 0;

    for (const Played & p : input) {
        inOnPerPass += (p.on && (p.ppq < kPass)) ? 1 : 0;
    }
    printf("input: %d notes a pass\n", inOnPerPass);
    int failures = 0;

    for (int pass = 0; pass < kPasses; pass++) {
        std::vector<Played> ons;

        for (const Played & p : output) {
            if (p.on && (p.ppq >= (pass * kPass) - 1e-9) && (p.ppq < ((pass + 1) * kPass) - 1e-9)) {
                ons.push_back(p);
            }
        }
        int expected = (pass == 0) ? inOnPerPass : (2 * inOnPerPass);
        int ratchets = 0;

        for (const Played & p : ons) {
            double beat = fmod(p.ppq, 0.5);

            ratchets += (fabs(beat - 0.25) < 1e-3) ? 1 : 0;
        }
        bool ok = ((int)ons.size() == expected) && (ratchets == ((pass == 0) ? 0 : inOnPerPass));

        failures += ok ? 0 : 1;
        printf("pass %d: %3zu note-ons (expected %3d), %3d a 16th after a note (expected %3d)  %s\n", pass + 1,
               ons.size(), expected, ratchets, (pass == 0) ? 0 : inOnPerPass, ok ? "ok" : "WRONG");
    }
    std::map<int, int> balance;

    for (const Played & p : output) {
        balance[p.note] += p.on ? 1 : -1;
    }
    for (auto & kv : balance) {
        if (kv.second != 0) {
            printf("HUNG: note %d is %+d on at the end\n", kv.first, kv.second);
            failures++;
        }
    }
    printf("first generated notes of pass 2:\n");
    int shown = 0;

    for (const Played & p : output) {
        if ((p.ppq >= kPass) && (shown < 8)) {
            printf("   %8.4f  %-3s note %3d  vel %3d\n", p.ppq, p.on ? "on" : "off", p.note, p.velocity);
            shown++;
        }
    }
    printf("%s\n", (failures == 0) ? "ALL OK" : "FAILED");
    return (failures == 0) ? 0 : 1;
}

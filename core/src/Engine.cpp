#include "mangle/Engine.h"

#include <cstring>

namespace mangle {

namespace {
inline double frac(double x) noexcept { return x - std::floor(x); }
inline float finiteOr0(float x) noexcept { return std::isfinite(x) ? x : 0.f; }
} // namespace

float Engine::modSum(const Settings& s, const float* lfo, ModTarget t) noexcept
{
    float sum = 0.f;
    for (int i = 0; i < kNumLfos; ++i)
        if (s.lfo[i].target == static_cast<int>(t) && t != ModTarget::None) sum += s.lfo[i].depth * lfo[i];
    return sum;
}

void Engine::prepare(double sampleRate, int maxBlock)
{
    sr_ = sampleRate > 0 ? sampleRate : 44100.0;
    maxBlock_ = std::max(1, maxBlock);
    shifter_.prepare(sr_, 0x1234abcdu);
    granular_.prepare(sr_, 0x9876fedcu);
    grit_.prepare(sr_);
    filter_.prepare(sr_);
    repeat_.prepare(sr_);
    reverb_.prepare(sr_);
    for (auto& t : tmp_) t.assign(static_cast<size_t>(kSubBlock), 0.f);
    prepared_ = true;
    reset();
}

void Engine::reset() noexcept
{
    for (auto& v : voices_) v = Voice {};
    heldCount_ = 0;
    loopGate_ = 0.f;
    newestVoice_ = -1;
    shifter_.reset();
    granular_.reset();
    grit_.reset();
    filter_.reset();
    repeat_.reset();
    reverb_.reset();
    sGrainMix_.set(0.f); sPan_.set(0.f); sOut_.set(1.f); sDryWet_.set(1.f); sRevMix_.set(0.f); sGrit_.set(0.f);
    sCut_.set(18000.f); sReso_.set(0.2f); sRepMix_.set(1.f);
    directBlend_ = 1.f;
    envState_ = 1.f;
    status_ = EngineStatus {};
}

void Engine::handleEvent(const Settings& s, const MidiEvent& e) noexcept
{
    switch (e.type)
    {
        case MidiEvent::NoteOn:
        {
            ++heldCount_;
            lastNote_ = e.note;
            int slot = -1;
            for (int i = 0; i < kVoices; ++i) if (!voices_[static_cast<size_t>(i)].active) { slot = i; break; }
            if (slot < 0)
            {
                float lowest = 2.f;
                for (int i = 0; i < kVoices; ++i)
                    if (voices_[static_cast<size_t>(i)].env < lowest) { lowest = voices_[static_cast<size_t>(i)].env; slot = i; }
            }
            Voice& v = voices_[static_cast<size_t>(slot)];
            v = Voice {};
            v.active = true;
            v.held = true;
            v.note = e.note;
            v.vel = std::clamp(e.velocity, 0.f, 1.f);
            v.env = 0.f;
            v.pos = 0.0;   // set to region start on first render (needs the sample)
            v.ended = false;
            newestVoice_ = slot;
            break;
        }
        case MidiEvent::NoteOff:
            heldCount_ = std::max(0, heldCount_ - 1);
            if (!s.oneShot)
                for (auto& v : voices_)
                    if (v.active && v.held && v.note == e.note) { v.held = false; break; }
            break;
        case MidiEvent::AllNotesOff:
            heldCount_ = 0;
            for (auto& v : voices_) v.held = false;
            break;
    }
}

void Engine::process(const SampleData* sample, const Settings& s, const Pattern& pat, const HostInfo& host,
                     const MidiEvent* events, int numEvents, float* outL, float* outR, int n) noexcept
{
    if (!prepared_ || n <= 0) return;
    const double bpm = host.valid && host.bpm > 1.0 ? host.bpm : std::clamp(s.fallbackBpm, 20.0, 400.0);
    const bool hostPlaying = !host.valid || host.playing;   // no host timeline (standalone): always running
    if (host.valid && host.playing) ppqNow_ = host.ppq;

    int evIdx = 0;
    int pos = 0;
    while (pos < n)
    {
        while (evIdx < numEvents && events[evIdx].offset <= pos)
        {
            handleEvent(s, events[evIdx]);
            if (s.mode == static_cast<int>(PlayMode::Keys) && events[evIdx].type == MidiEvent::NoteOn && sample != nullptr && !sample->empty())
            {
                const auto reg = regionFor(*sample, s.sampleStart, s.sampleEnd);
                for (auto& v : voices_) if (v.active && v.pos == 0.0 && v.env == 0.f) v.pos = reg.start;
            }
            ++evIdx;
        }
        int end = std::min(n, pos + kSubBlock);
        if (evIdx < numEvents && events[evIdx].offset > pos) end = std::min(end, events[evIdx].offset);
        renderSub(sample, s, pat, bpm, hostPlaying, outL + pos, outR + pos, end - pos);
        pos = end;
    }
    while (evIdx < numEvents) handleEvent(s, events[evIdx++]);
}

void Engine::renderSub(const SampleData* d, const Settings& s, const Pattern& pat, double bpm, bool hostPlaying,
                       float* outL, float* outR, int n) noexcept
{
    const double updRate = sr_ / kSubBlock;
    const double ppq0 = ppqNow_;
    const double ppqPerSample = bpm / 60.0 / sr_;
    const double ppqMid = ppq0 + 0.5 * n * ppqPerSample;
    const bool have = d != nullptr && !d->empty();
    const bool keys = s.mode == static_cast<int>(PlayMode::Keys);
    const bool midiLoop = s.mode == static_cast<int>(PlayMode::LoopMidi);

    // ---- LFOs -------------------------------------------------------------------------------------
    for (int i = 0; i < kNumLfos; ++i)
        status_.lfo[i] = s.lfo[i].depth != 0.f && s.lfo[i].target != 0
                             ? lfoValue(s.lfo[i].shape, ppqMid, lfoBeatsForRate(s.lfo[i].rate)) : 0.f;
    const float* lfo = status_.lfo;
    auto mod = [&](ModTarget t) { return modSum(s, lfo, t); };

    // ---- sequencer --------------------------------------------------------------------------------
    const double stepBeats = stepBeatsForRate(s.seqRate);
    const StepPos sp = stepAt(ppqMid, stepBeats, s.swing, pat.length);
    float laneGate = 1.f, laneFilter = 0.f;
    int laneSemis = 0, laneRepeat = 0;
    if (s.seqOn)
    {
        laneGate = std::clamp(pat.v[LaneGate][sp.index], 0.f, 1.f);
        laneSemis = lanePitchSemis(pat.v[LanePitch][sp.index]);
        laneFilter = std::clamp(pat.v[LaneFilter][sp.index], 0.f, 1.f);
        laneRepeat = laneRepeatIndex(pat.v[LaneRepeat][sp.index]);
    }
    status_.step = sp.index;

    // ---- pitch bookkeeping ------------------------------------------------------------------------
    const double midiSemis = heldCount_ > 0 ? static_cast<double>(lastNote_ - s.rootNote) : 0.0;
    const double semisBase = static_cast<double>(s.semis) + static_cast<double>(s.fineCents) * 0.01 + laneSemis
                           + static_cast<double>(mod(ModTarget::Pitch)) * 12.0;
    const double natural = have ? d->sampleRate / sr_ : 1.0;
    const Region reg = have ? regionFor(*d, s.sampleStart, s.sampleEnd) : Region {};

    float* srcL = tmp_[0].data();
    float* srcR = tmp_[1].data();
    std::memset(srcL, 0, sizeof(float) * static_cast<size_t>(n));
    std::memset(srcR, 0, sizeof(float) * static_cast<size_t>(n));
    double anchor = 0.0, anchorInc = 0.0;   // for granular
    bool sourceActive = false;

    const float relCoef = s.releaseMs <= 1.f ? 0.f : std::exp(-6.9f / (s.releaseMs * 0.001f * static_cast<float>(sr_)));
    const float atkCoef = std::exp(-6.9f / (0.004f * static_cast<float>(sr_)));

    if (!keys)
    {
        // ---- tempo-locked loop ---------------------------------------------------------------------
        const bool wantOn = midiLoop ? heldCount_ > 0 : hostPlaying;
        for (int i = 0; i < n; ++i)
        {
            loopGate_ = wantOn ? 1.f - atkCoef * (1.f - loopGate_) : loopGate_ * relCoef;
            if (loopGate_ < 1e-5f) loopGate_ = 0.f;
        }
        sourceActive = loopGate_ > 0.f;
        if (have && sourceActive)
        {
            const double loopSamples = std::max(1.0, static_cast<double>(std::max(0.25f, s.loopBeats)) * 60.0 / bpm * sr_);
            const double stretch = reg.length / loopSamples;                 // file frames per output sample
            const double semisTot = semisBase + midiSemis;
            const double filePos0 = reg.start + frac(ppq0 / static_cast<double>(std::max(0.25f, s.loopBeats))) * reg.length;
            const bool directOk = std::abs(semisTot) < 1e-3 && std::abs(stretch / natural - 1.0) < 0.002;
            const float target = directOk ? 1.f : 0.f;
            directBlend_ += (target - directBlend_) * 0.12f;
            if (std::abs(directBlend_ - target) < 1e-3f) directBlend_ = target;
            anchor = filePos0;
            anchorInc = stretch;
            if (directBlend_ > 0.f)
                for (int i = 0; i < n; ++i)
                {
                    float l, r;
                    readWrapped(*d, reg, filePos0 + i * stretch, l, r);
                    srcL[i] = l * directBlend_;
                    srcR[i] = r * directBlend_;
                }
            if (directBlend_ < 1.f)
            {
                float* gl = tmp_[2].data();
                float* gr = tmp_[3].data();
                GrainEngine::Params gp;
                gp.sizeSamples = 2048.0 * sr_ / 44100.0;
                gp.overlap = 3.f;
                gp.pitchRatio = natural * std::pow(2.0, semisTot / 12.0);
                gp.align = true;
                shifter_.process(*d, reg, filePos0, stretch, gp, gl, gr, n, true);
                const float gb = 1.f - directBlend_;
                for (int i = 0; i < n; ++i) { srcL[i] += gl[i] * gb; srcR[i] += gr[i] * gb; }
            }
            for (int i = 0; i < n; ++i)   // amplitude (release / attack) happens after the pitch stage
            {
                // loopGate_ is per sub-block; ramp is already smooth at 4 ms
                srcL[i] *= loopGate_;
                srcR[i] *= loopGate_;
            }
        }
    }
    else
    {
        // ---- keyboard sampler ------------------------------------------------------------------------
        for (size_t vi = 0; vi < voices_.size(); ++vi)
        {
            Voice& v = voices_[vi];
            if (!v.active) continue;
            if (!have) { v.active = false; continue; }
            sourceActive = true;
            const double semisTot = semisBase + static_cast<double>(v.note - s.rootNote);
            const double inc = natural * std::pow(2.0, semisTot / 12.0);
            const double endPos = reg.start + reg.length;
            for (int i = 0; i < n; ++i)
            {
                if (s.oneShot && v.pos >= endPos) v.ended = true;
                if (v.held && !v.ended) v.env = 1.f - atkCoef * (1.f - v.env);
                else v.env *= (v.ended ? std::min(relCoef, 0.995f) : relCoef);
                if (!v.held && !s.oneShot && relCoef == 0.f) v.env = 0.f;
                if (v.env < 1e-4f && (!v.held || v.ended)) { v.active = false; break; }
                float l, r;
                readWrapped(*d, reg, v.pos, l, r);
                const float g = v.env * v.vel;
                srcL[i] += l * g;
                srcR[i] += r * g;
                v.pos += inc;
                if (!s.oneShot && v.pos >= endPos) v.pos -= reg.length;
            }
            if (static_cast<int>(vi) == newestVoice_ && v.active) { anchor = v.pos; anchorInc = inc; }
        }
        if (newestVoice_ >= 0 && !voices_[static_cast<size_t>(newestVoice_)].active) newestVoice_ = -1;
    }

    // dry tap (source after the pitch stage, before every effect)
    float* dryL = tmp_[4].data();
    float* dryR = tmp_[5].data();
    std::memcpy(dryL, srcL, sizeof(float) * static_cast<size_t>(n));
    std::memcpy(dryR, srcR, sizeof(float) * static_cast<size_t>(n));

    // ---- granular effect --------------------------------------------------------------------------
    sGrainMix_.target = std::clamp(s.grainMix + mod(ModTarget::GrainMix), 0.f, 1.f);
    const float gmix = sGrainMix_.step(smoothCoeff(0.02, updRate));
    const bool grainsAlive = granular_.activeGrains() > 0;
    if (have && (gmix > 0.001f || sGrainMix_.target > 0.001f) && (sourceActive || grainsAlive) && anchorInc > 0.0)
    {
        float* gl = tmp_[6].data();
        float* gr = tmp_[7].data();
        const float scatter = std::clamp(s.grainScatter + mod(ModTarget::GrainScatter), 0.f, 1.f);
        GrainEngine::Params gp;
        gp.sizeSamples = std::clamp(static_cast<double>(s.grainSizeMs), 5.0, 500.0) * 0.001 * sr_;
        gp.overlap = std::clamp(s.grainDensity, 1.f, 12.f);
        const double gsemis = static_cast<double>(s.grainPitch + mod(ModTarget::GrainPitch) * 12.f);
        gp.pitchRatio = natural * std::pow(2.0, (gsemis + (keys ? 0.0 : semisBase + midiSemis)) / 12.0);
        gp.scatterFrames = static_cast<double>(scatter) * 0.25 * d->sampleRate;
        gp.pitchRandomSemis = scatter * 3.f;
        gp.spread = scatter;
        granular_.process(*d, reg, anchor, anchorInc, gp, gl, gr, n, true);
        const float gg = sourceActive ? 1.f : 0.f;
        for (int i = 0; i < n; ++i)
        {
            srcL[i] = srcL[i] * (1.f - gmix) + gl[i] * gmix * (keys ? 1.f : loopGate_ > 0.f ? loopGate_ : gg);
            srcR[i] = srcR[i] * (1.f - gmix) + gr[i] * gmix * (keys ? 1.f : loopGate_ > 0.f ? loopGate_ : gg);
        }
    }

    // ---- gate (step sequencer + shape / edges / depth) -----------------------------------------------
    float gateTarget = 1.f;
    if (s.seqOn)
    {
        const float depth = std::clamp(s.gateDepth + mod(ModTarget::GateDepth), 0.f, 1.f);
        const float duty = 0.08f + 0.92f * std::clamp(s.gateShape, 0.f, 1.f);
        const float openLevel = 1.f - depth * (1.f - laneGate);
        const float closedLevel = 1.f - depth;
        gateTarget = laneGate > 0.f && sp.phase < static_cast<double>(duty) ? openLevel : closedLevel;
    }
    const double stepSeconds = stepBeats * 60.0 / bpm;
    const double edgeSeconds = std::max(0.0004, static_cast<double>(std::clamp(s.gateEdges, 0.f, 1.f)) * 0.45 * stepSeconds);
    const float gateCoef = static_cast<float>(1.0 - std::exp(-1.0 / (edgeSeconds * sr_)));
    for (int i = 0; i < n; ++i)
    {
        envState_ += (gateTarget - envState_) * gateCoef;
        srcL[i] *= envState_;
        srcR[i] *= envState_;
    }

    // ---- per-sample tail: grit -> filter -> repeat -> reverb -> mix -> pan/out --------------------------
    sGrit_.target = std::clamp(s.grit + mod(ModTarget::Grit), 0.f, 1.f);
    const float grit = sGrit_.step(smoothCoeff(0.02, updRate));
    const float cutMod = mod(ModTarget::FilterFreq) * 4.f + laneFilter * 4.f;   // octaves
    sCut_.target = std::clamp(s.filterFreq * std::pow(2.f, cutMod), 20.f, 20000.f);
    sReso_.target = std::clamp(s.filterReso + mod(ModTarget::FilterReso), 0.f, 1.f);
    const float cut = sCut_.step(smoothCoeff(0.015, updRate));
    const float reso = sReso_.step(smoothCoeff(0.015, updRate));
    const bool hp = s.filterType == 1;
    const bool filterNeeded = hp ? cut > 21.f : cut < 19000.f || reso > 0.35f;
    if (filterNeeded) filter_.set(cut, reso, hp);

    int repIdx = laneRepeat > 0 ? laneRepeat : s.repeatDiv;
    const double repBeats = repIdx > 0 ? repeatBeatsForDiv(repIdx) : 0.0;
    sRepMix_.target = std::clamp(s.repeatMix + mod(ModTarget::RepeatMix), 0.f, 1.f);
    const float repMix = sRepMix_.step(smoothCoeff(0.01, updRate));

    sRevMix_.target = std::clamp(s.revMix + mod(ModTarget::ReverbMix), 0.f, 1.f);
    const float revMix = sRevMix_.step(smoothCoeff(0.03, updRate));
    sDryWet_.target = std::clamp(s.dryWet + mod(ModTarget::DryWet), 0.f, 1.f);
    const float dw = sDryWet_.step(smoothCoeff(0.02, updRate));
    sPan_.target = std::clamp(s.pan + mod(ModTarget::Pan), -1.f, 1.f);
    const float pan = sPan_.step(smoothCoeff(0.02, updRate));
    sOut_.target = dbToGain(std::clamp(s.outDb + mod(ModTarget::Out) * 12.f, -60.f, 12.f));
    const float outG = sOut_.step(smoothCoeff(0.02, updRate));
    const float gl = std::min(1.f, 1.f - pan) * outG, gr = std::min(1.f, 1.f + pan) * outG;
    const float revAmt = revMix * 0.5f;

    float peak = 0.f;
    for (int i = 0; i < n; ++i)
    {
        float l = srcL[i], r = srcR[i];
        if (grit > 0.f) grit_.process(l, r, grit);
        if (filterNeeded) filter_.process(l, r);
        repeat_.process(l, r, repBeats, bpm, s.repeatGate, repMix);
        if (revAmt > 0.0005f || revMix > 0.0005f)
        {
            float wl, wr;
            reverb_.process(l, r, wl, wr, s.revSize, s.revDamp);
            l += wl * revAmt * 2.f;
            r += wr * revAmt * 2.f;
        }
        l = dryL[i] * (1.f - dw) + l * dw;
        r = dryR[i] * (1.f - dw) + r * dw;
        l = finiteOr0(l * gl);
        r = finiteOr0(r * gr);
        outL[i] = l;
        outR[i] = r;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }

    // ---- status -----------------------------------------------------------------------------------
    ppqNow_ = ppq0 + n * ppqPerSample;
    status_.ppq = ppqNow_;
    status_.level = peak;
    status_.repeating = repeat_.active();
    if (keys)
    {
        status_.loopPhase = 0.0;
        status_.playing = sourceActive;
        if (have && newestVoice_ >= 0) status_.loopPhase = std::clamp((voices_[static_cast<size_t>(newestVoice_)].pos - reg.start) / reg.length, 0.0, 1.0);
    }
    else
    {
        status_.playing = loopGate_ > 0.f;
        status_.loopPhase = frac(ppqNow_ / static_cast<double>(std::max(0.25f, s.loopBeats)));
    }
}

} // namespace mangle

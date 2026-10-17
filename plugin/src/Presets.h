#pragma once
// Factory presets (exposed to Logic as the plug-in's programs) and the Randomize helper.

#include <juce_audio_processors/juce_audio_processors.h>

namespace mangle::presets {

int count();
juce::String name(int index);
/** Resets every parameter to its default, then applies the preset. Message thread. */
void apply(juce::AudioProcessorValueTreeState& apvts, int index);
/** Musical randomisation of the creative parameters and the step lanes (keeps the loop length / mode / sample). */
void randomize(juce::AudioProcessorValueTreeState& apvts, juce::Random& rng);
/** Restore every parameter except the sample-related ones to its default. */
void resetAll(juce::AudioProcessorValueTreeState& apvts);

} // namespace mangle::presets

/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class PingPongDelayAdvancedAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    PingPongDelayAdvancedAudioProcessorEditor (PingPongDelayAdvancedAudioProcessor&);
    ~PingPongDelayAdvancedAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Slider mDryWetSlider;
    Slider mFeedbackSlider;
    Slider mDelayTimeSlider;
    Slider mSpreadSlider;


    PingPongDelayAdvancedAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingPongDelayAdvancedAudioProcessorEditor)
};

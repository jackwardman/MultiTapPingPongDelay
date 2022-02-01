/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PingPongDelayAdvancedAudioProcessor::PingPongDelayAdvancedAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    addParameter(mDryWetParameter = new AudioParameterFloat("drywet",
                                                            "Dry Wet",
                                                            0.0f,
                                                            1.0f,
                                                            0.5f));
    
    addParameter(mFeedbackParameter = new AudioParameterFloat("feedback",
                                                              "Feedback",
                                                              0.0f,
                                                              0.98f,
                                                              0.5f));
    
    addParameter(mTimeParameter = new AudioParameterFloat("delaytime",
                                                          "Delay Time",
                                                          0.01f,
                                                          MAX_DELAY_TIME,
                                                          1.0f));
    
    addParameter(mSpreadParameter = new AudioParameterFloat("spread",
                                                          "Spread",
                                                          0.f,
                                                          2000.f,
                                                          5.0f));
    
    

    mTimeSmoothed = 0;
    mDryWetSmoothed = 0;
    mSpreadSmoothed = 0;
    mCircularBufferLeft = nullptr;
    mCircularBufferRight = nullptr;
    mWriteHead = 0;
    mCircularBufferLength = 0;
    mDelayTimeInSamples = 0;
    mReadHead1 = 0;
    mReadHead2 = 0;
    mReadHead3 = 0;
    mReadHead4 = 0;
    mFeedbackLeft = 0;
    mFeedbackRight = 0;
    mDryWet = 0.5;
    
}

PingPongDelayAdvancedAudioProcessor::~PingPongDelayAdvancedAudioProcessor()
{
    if (mCircularBufferLeft != nullptr) {
        delete[] mCircularBufferLeft;
        mCircularBufferLeft = nullptr;
   }
    if (mCircularBufferRight != nullptr) {
        delete[] mCircularBufferRight;
        mCircularBufferRight = nullptr;
    }
}

//==============================================================================
const juce::String PingPongDelayAdvancedAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PingPongDelayAdvancedAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PingPongDelayAdvancedAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PingPongDelayAdvancedAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PingPongDelayAdvancedAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PingPongDelayAdvancedAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PingPongDelayAdvancedAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PingPongDelayAdvancedAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String PingPongDelayAdvancedAudioProcessor::getProgramName (int index)
{
    return {};
}

void PingPongDelayAdvancedAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void PingPongDelayAdvancedAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mDelayTimeInSamples = sampleRate * *mTimeParameter;
    
    // Calculate the length of the buffer based on sample rate
    mCircularBufferLength = sampleRate * MAX_DELAY_TIME;
    
    // Dynamically instantiate the arrays for the circular buffers
     if (mCircularBufferLeft == nullptr) {
        mCircularBufferLeft = new float[mCircularBufferLength];
    }
    // delete any junk inisde the buffers
    zeromem(mCircularBufferLeft, mCircularBufferLength * sizeof(float));

    if (mCircularBufferRight == nullptr) {
        mCircularBufferRight = new float[mCircularBufferLength];
    }
    zeromem(mCircularBufferRight, mCircularBufferLength * sizeof(float));
    
    mWriteHead = 0;
    
    mTimeSmoothed = *mTimeParameter;
    mDryWetSmoothed = *mDryWetParameter;
    mSpreadSmoothed = *mSpreadParameter;
}

void PingPongDelayAdvancedAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PingPongDelayAdvancedAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void PingPongDelayAdvancedAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

   
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    
    for (int i = 0; i < buffer.getNumSamples(); i++) {
        
        // Smooth time and dry wet parameters to avoid discontinuities when changing the values
        mDryWetSmoothed = mDryWetSmoothed - 0.0001 * (mDryWetSmoothed - *mDryWetParameter);
        mTimeSmoothed = mTimeSmoothed - 0.0001 * (mTimeSmoothed - *mTimeParameter);
        mSpreadSmoothed = mSpreadSmoothed - 0.0001 * (mSpreadSmoothed - *mSpreadParameter);
        
        mDelayTimeInSamples = getSampleRate() * mTimeSmoothed;
        
        // Write the input and feed back signal to the buffer at the write index
        mCircularBufferLeft[mWriteHead] = rightChannel[i] + mFeedbackRight;
        mCircularBufferRight[mWriteHead] = leftChannel[i] + mFeedbackLeft;
        // Define the read index as number of samples behind the write pointer
        mReadHead1 = mWriteHead - mDelayTimeInSamples;
        // Define the subsequent read indexes with non linear spacing multiplied with the spread parameter to define the overall spread between the indexes
        mReadHead2 = mReadHead1 + (2 * mSpreadSmoothed);
        mReadHead3 = mReadHead1 + (7 * mSpreadSmoothed);
        mReadHead4 = mReadHead1 + (11 * mSpreadSmoothed);

        // If the pointers go lower than 0, wrap it round by adding the length of buffer back to the index
        if(mReadHead1 < 0)
            mReadHead1 += mCircularBufferLength;
        // Wrap read indexes
        if(mReadHead1 >= mCircularBufferLength)
            mReadHead1 -= mCircularBufferLength;
        
        if(mReadHead2 < 0)
            mReadHead2 += mCircularBufferLength;
        if(mReadHead2 >= mCircularBufferLength)
            mReadHead2 -= mCircularBufferLength;
        
        if(mReadHead3 < 0)
            mReadHead3 += mCircularBufferLength;
        if(mReadHead3 >= mCircularBufferLength)
            mReadHead3 -= mCircularBufferLength;
        
        if(mReadHead4 < 0)
            mReadHead4 += mCircularBufferLength;
        if(mReadHead4 >= mCircularBufferLength)
            mReadHead4 -= mCircularBufferLength;
        
        // Initialise interpolation parameters
        int readHead1_x = (int)mReadHead1;
        int readHead1_x1 = readHead1_x + 1;
        float readHeadFloat1 = mReadHead1 - readHead1_x;
        if (readHead1_x1 >= mCircularBufferLength)
            readHead1_x1 -= mCircularBufferLength;
        
        int readHead2_x = (int)mReadHead2;
        int readHead2_x1 = readHead2_x + 1;
        float readHeadFloat2 = mReadHead2 - readHead2_x;
        if (readHead2_x1 >= mCircularBufferLength)
            readHead2_x1 -= mCircularBufferLength;
        
        int readHead3_x = (int)mReadHead3;
        int readHead3_x1 = readHead3_x + 1;
        float readHeadFloat3 = mReadHead3 - readHead3_x;
        if (readHead3_x1 >= mCircularBufferLength)
            readHead3_x1 -= mCircularBufferLength;
        
        int readHead4_x = (int)mReadHead4;
        int readHead4_x1 = readHead4_x + 1;
        float readHeadFloat4 = mReadHead4 - readHead4_x;
        if (readHead4_x1 >= mCircularBufferLength)
            readHead4_x1 -= mCircularBufferLength;

        // Apply interpolation to get interpolated sample value
        float tap_left1 = lin_interp(mCircularBufferLeft[readHead1_x], mCircularBufferLeft[readHead1_x1], readHeadFloat1);
        float tap_right1 = lin_interp(mCircularBufferRight[readHead1_x], mCircularBufferRight[readHead1_x1], readHeadFloat1);
        float tap_left2 = lin_interp(mCircularBufferLeft[readHead2_x], mCircularBufferLeft[readHead2_x1], readHeadFloat2);
        float tap_right2 = lin_interp(mCircularBufferRight[readHead2_x], mCircularBufferRight[readHead2_x1], readHeadFloat2);
        float tap_left3 = lin_interp(mCircularBufferLeft[readHead3_x], mCircularBufferLeft[readHead3_x1], readHeadFloat3);
        float tap_right3 = lin_interp(mCircularBufferRight[readHead3_x], mCircularBufferRight[readHead3_x1], readHeadFloat3);
        float tap_left4 = lin_interp(mCircularBufferLeft[readHead4_x], mCircularBufferLeft[readHead4_x1], readHeadFloat4);
        float tap_right4 = lin_interp(mCircularBufferRight[readHead4_x], mCircularBufferRight[readHead4_x1], readHeadFloat4);
        
        // Weight and sum the tap outputs
        float delay_sample_left =(0.3f*tap_left1) + (0.6f*tap_left2) + (-0.2f*tap_left3) + (0.1f*tap_left4);
        float delay_sample_right = (0.3f*tap_right1) + (0.6f*tap_right2) + (-0.2f*tap_right3) + (0.1f*tap_right4);
        
        // Feedback value
        mFeedbackLeft = delay_sample_left * *mFeedbackParameter;
        mFeedbackRight = delay_sample_right * *mFeedbackParameter;
        
        // Sum the dry and wet signals
        buffer.setSample(0, i, (1 - mDryWetSmoothed) * buffer.getSample(0, i) + delay_sample_left *  mDryWetSmoothed);
        buffer.setSample(1, i, (1 - mDryWetSmoothed) * buffer.getSample(1, i) + delay_sample_right *  mDryWetSmoothed);
        
        // Increment and wrap write index
        mWriteHead++;
        if (mWriteHead >= mCircularBufferLength)
            mWriteHead = 0;

    }
}

//==============================================================================
bool PingPongDelayAdvancedAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PingPongDelayAdvancedAudioProcessor::createEditor()
{
    return new PingPongDelayAdvancedAudioProcessorEditor (*this);
}

//==============================================================================
void PingPongDelayAdvancedAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::unique_ptr<XmlElement> xml(new XmlElement("PingPongDelay"));
    xml-> setAttribute("drywet", *mDryWetParameter);
    xml->setAttribute("feedback", *mFeedbackParameter);
    xml->setAttribute("delaytime", *mTimeParameter);
    xml->setAttribute("spread", *mSpreadParameter);
    
    copyXmlToBinary(*xml, destData);
}

void PingPongDelayAdvancedAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml.get() != nullptr && xml->hasTagName("PingPongDelay")) {
        *mDryWetParameter = xml->getDoubleAttribute("drywet");
        *mFeedbackParameter = xml->getDoubleAttribute("feedback");
        *mTimeParameter = xml->getDoubleAttribute("delaytime");
        *mSpreadParameter = xml->getDoubleAttribute("spread");
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PingPongDelayAdvancedAudioProcessor();
}

float PingPongDelayAdvancedAudioProcessor::lin_interp(float inX, float inX1, float inPhase)
{
    return (1 - inPhase) * inX + inPhase * inX1;
}

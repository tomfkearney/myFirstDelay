/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MyFirstDelayAudioProcessor::MyFirstDelayAudioProcessor()
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
    
    addParameter( mDryWetParameter = new juce::AudioParameterFloat(
                                                          juce::ParameterID("drywet", 1),
                                                          "dry wet",
                                                          0.0,
                                                          1.0,
                                                          0.5
                                                          ));
    addParameter( mFeedbackParameter = new juce::AudioParameterFloat(
                                                          juce::ParameterID("feedback", 1),
                                                          "feedback",
                                                          0,
                                                          0.98,
                                                          0.5
                                                          ));
    addParameter( mDelayTimeParameter = new juce::AudioParameterFloat(
                                                          juce::ParameterID("delaytime", 1),
                                                          "delay time",
                                                          0.01,
                                                          MAX_DELAY_TIME,
                                                          0.5
                                                          ));
    
    mDelayTimeSmoothed = 0;
    mCircularBufferLeft = nullptr;
    mCircularBufferRight = nullptr;
    mCircularBufferWriteHead = 0;
    mCircularBufferLength = 0;
    mDelayTimeInSamples = 0;
    mDelayReadHead = 0;
    
    mFeedbackLeft = 0;
    mFeedbackRight = 0;
    
}

MyFirstDelayAudioProcessor::~MyFirstDelayAudioProcessor()
{
    if(mCircularBufferLeft != nullptr)
    {
        delete[] mCircularBufferLeft;
        mCircularBufferLeft = nullptr;
    }
    
    if(mCircularBufferRight != nullptr)
    {
        delete[] mCircularBufferRight;
        mCircularBufferRight = nullptr;
    }
    
}

//==============================================================================
const juce::String MyFirstDelayAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MyFirstDelayAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool MyFirstDelayAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool MyFirstDelayAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double MyFirstDelayAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MyFirstDelayAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int MyFirstDelayAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MyFirstDelayAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String MyFirstDelayAudioProcessor::getProgramName (int index)
{
    return {};
}

void MyFirstDelayAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void MyFirstDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    mCircularBufferLength = sampleRate * MAX_DELAY_TIME;
    mDelayTimeInSamples = sampleRate * 0.5;
    
    if(mCircularBufferLeft == nullptr)
    {
        mCircularBufferLeft = new float[mCircularBufferLength];
    }
    
    juce::zeromem(mCircularBufferLeft, mCircularBufferLength * sizeof(float));
    
    if(mCircularBufferRight == nullptr)
    {
        mCircularBufferRight = new float[mCircularBufferLength];
    }
    
    juce::zeromem(mCircularBufferRight, mCircularBufferLength * sizeof(float));
    
    mCircularBufferWriteHead = 0;
    
    mDelayTimeSmoothed = *mDelayTimeParameter;

}

void MyFirstDelayAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MyFirstDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void MyFirstDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    

    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);
    
    for(int i = 0; i < buffer.getNumSamples(); i++)
    {
        mDelayTimeSmoothed = mDelayTimeSmoothed - 0.001 * (mDelayTimeSmoothed - *mDelayTimeParameter);
        
        mDelayTimeInSamples = getSampleRate() * mDelayTimeSmoothed;
        
        mCircularBufferLeft[mCircularBufferWriteHead] = leftChannel[i] + mFeedbackLeft;
        mCircularBufferRight[mCircularBufferWriteHead] = rightChannel[i] + mFeedbackRight;
        
        mDelayReadHead = mCircularBufferWriteHead - mDelayTimeInSamples;
        
        if(mDelayReadHead < 0 )
        {
            mDelayReadHead += mCircularBufferLength;
        }
        
        int readHead_x = (int)mDelayReadHead;
        int readHead_x1 = readHead_x + 1;
        if(readHead_x1 >= mCircularBufferLength){
            readHead_x1 -= mCircularBufferLength;
        }
        
        float readHeadFloat = mDelayReadHead - readHead_x;
        
        float delay_sample_left = lin_interp(mCircularBufferLeft[readHead_x],mCircularBufferLeft[readHead_x1], readHeadFloat);
        float delay_sample_right = lin_interp(mCircularBufferRight[readHead_x],mCircularBufferRight[readHead_x1], readHeadFloat);
        
        mFeedbackLeft = delay_sample_left * 0.8;
        mFeedbackRight = delay_sample_right * 0.8;
        
        mCircularBufferWriteHead++;
        
        buffer.setSample(0, i, buffer.getSample(0, i) * (1 - *mDryWetParameter) + delay_sample_left * *mDryWetParameter);
        buffer.setSample(1, i, buffer.getSample(1, i) * (1 - *mDryWetParameter) + delay_sample_left * *mDryWetParameter);
        
//        buffer.addSample(0, i, delay_sample_left);
//        buffer.addSample(1, i, delay_sample_right);
    
        mCircularBufferWriteHead++;
        
        if(mCircularBufferWriteHead >= mCircularBufferLength)
        {
            mCircularBufferWriteHead = 0;
        }
    }
}

float MyFirstDelayAudioProcessor::lin_interp(float sample_x, float sample_x1, float inPhase)
{
    return (1 - inPhase) * sample_x + inPhase * sample_x1;
}

//==============================================================================
bool MyFirstDelayAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* MyFirstDelayAudioProcessor::createEditor()
{
    return new MyFirstDelayAudioProcessorEditor (*this);
}

//==============================================================================
void MyFirstDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void MyFirstDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyFirstDelayAudioProcessor();
}

/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"


//==============================================================================
SimpleTunerAudioProcessor::SimpleTunerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)
                     #endif
                       )
#endif
{
}

SimpleTunerAudioProcessor::~SimpleTunerAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleTunerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleTunerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleTunerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleTunerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleTunerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleTunerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleTunerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleTunerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleTunerAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleTunerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleTunerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    //Initialize FIFO buffers.
    bufferFifo.prepare(samplesPerBlock);
    audioBufferForFFT.setSize(1,fftDataStructure.getFFTSize());
    
    topFFTData.clear();
    topFFTData.resize(masterFFTLength, 0);
    nextFFTData.clear();
    nextFFTData.resize(masterFFTLength,0);
    
    currentExactF = -1.f;
    
}

void SimpleTunerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleTunerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void SimpleTunerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    //processBlock doesn't fetch audio data. The audio data is already in the buffer object, which is an argument.
    
    juce::ScopedNoDenormals noDenormals; //does something to address floating point tomfoolery with large/small numbers
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    for (int channel = 0; channel < totalNumInputChannels; ++channel) //There should only be 1 channel
    {

        bufferFifo.update(buffer); //Put the incoming audio into the sample FIFO
        
        while( bufferFifo.getNumCompleteBuffersAvailable() > 0)
        {
            //dummyBuffer holds the buffer we just pulled from the FIFO
            if ( bufferFifo.getAudioBuffer(dummyBuffer) )
            {
                int size = dummyBuffer.getNumSamples();
                
                
                //Shift the samples already in the audioBufferForFFT to the left to make room for dummyBuffer at the end
                juce::FloatVectorOperations::copy(audioBufferForFFT.getWritePointer(0, 0), //float* dest
                                                  audioBufferForFFT.getReadPointer(0,size),//const float* source
                                                  audioBufferForFFT.getNumSamples()-size//int numValues
                                                  );
                //Now insert the dummyBuffer at the end
                juce::FloatVectorOperations::copy(audioBufferForFFT.getWritePointer(0, audioBufferForFFT.getNumSamples()-size),
                                                  dummyBuffer.getReadPointer(0,0),
                                                  size);
                
                fftDataStructure.produceFFTData(audioBufferForFFT);
            }
        }
        
        //Now at least 1 FFT vector exists in the fftDataStructure. We shall use this to findExactF
        //We need the numAvailableFFTDataBlocks to be at least 2 in order to use pullTopViewNext.
        //We need pullTopViewNext to calculate the phase remainder in findExactMaxFrequency
        while( fftDataStructure.getNumAvailableFFTDataBlocks() > 1) //was 0
        {
            
            int fftPullStatus = fftDataStructure.pullTopViewNext(topFFTData, nextFFTData);
            if (fftPullStatus)
            {
                currentExactF = findExactMaxFrequency(topFFTData, nextFFTData);
            }
        }

        
    } //end for channel loop
} //end processBlock()

//==============================================================================
bool SimpleTunerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleTunerAudioProcessor::createEditor()
{
    return new SimpleTunerAudioProcessorEditor (*this);
}

//==============================================================================
void SimpleTunerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void SimpleTunerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}


template<typename DataType>
int SimpleTunerAudioProcessor::findComplexMaxIndex(std::vector<DataType>& fftDataVector)
{
    int maxIndex = 0;
    //basically the same as hypot
//    DataType maxElement = fftDataVector[maxIndex]*fftDataVector[maxIndex] + fftDataVector[maxIndex+1]*fftDataVector[maxIndex+1]; //pow(fftDV[0],2)+pow(fftDV[1],2)
//    maxElement = std::sqrt(maxElement);
    
    DataType maxElement = std::hypot(fftDataVector[maxIndex], fftDataVector[maxIndex+1]);
    
    
    DataType thisElement {};
    
    for (int i = 2; i < masterFFTLength; i = i+2) //index 1 is imag(DC)
    {

        
        thisElement = std::hypot(fftDataVector[i], fftDataVector[i+1]);
        
        if (maxElement < thisElement)
        {
            maxIndex = fundamentalFrequencyChecker(i, maxElement, fftDataVector); //i;
            maxElement = thisElement;
            
        }//end if
    }

    return maxIndex; //This is the index WHERE THE DATA IS. If we want the "structural" index, that would be maxIndex/2
}

template<typename DataType>
int SimpleTunerAudioProcessor::fundamentalFrequencyChecker(int i, float magI, std::vector<DataType>& fftDataVector)
{
    //Previously there was a bug where the tuner would report an incorrect note
    //if a harmonic is higher power than the fundamental
    //(eg it reports E-330Hz as the target note when playing A-110 on a guitar)
    //To address this, we check if this note is a 3rd, 5th, or 6th harmonic (highest power non-octave harmonics)
    
    //check to see if the index is divisible by 3, 5, or 6. If it is, there should be a clean peak in the FFT at that lower interval, so we would only need to check one point. If not, we have to check two indexes to see if there's an "unclean" peak
    //i is always even so we can int/2 without worry
    
    //When we determine that a lower harmonic is higher power, we replace magI instead of allocating another double
    
    int indexToReturn = i, tempIndexHolder;
    float largestMag = magI;
    
    tempIndexHolder = checkSpecificHarmonic(3, i, largestMag, fftDataVector);
    indexToReturn = (tempIndexHolder < indexToReturn) ? tempIndexHolder : indexToReturn; //If we find a local max at the LOWER index, replace it
    
    tempIndexHolder = checkSpecificHarmonic(5, i, largestMag, fftDataVector);
    indexToReturn = (tempIndexHolder < indexToReturn) ? tempIndexHolder : indexToReturn; //If we find a local max at the LOWER index, replace it
    
    tempIndexHolder = checkSpecificHarmonic(6, i, largestMag, fftDataVector);
    indexToReturn = (tempIndexHolder < indexToReturn) ? tempIndexHolder : indexToReturn; //If we find a local max at the LOWER index, replace it

    //static int counter = 0;
    //DBG(counter++ << " : " << "Replaced index " << i << " with index " << indexToReturn);
    return indexToReturn;
    
}

template<typename DataType>
int SimpleTunerAudioProcessor::checkSpecificHarmonic(int harmonicNumber, int i, float& magI, std::vector<DataType>& fftDataVector)
{
    //magI is a reference to avoid making destructor calls
    
    static int rootIndex1, rootIndex2;
    static float fftMag1, fftMag2, fftMagMin1, fftMagPlus1;
    
    if ( (i) % (2*harmonicNumber) == 0 )
    {
        
        rootIndex1 = i/harmonicNumber;
        rootIndex1 += (rootIndex1 & 1); //If it's even, do nothing. If it's odd, add 1 to make it even (round up always)
        fftMag1 = hypotf(fftDataVector[rootIndex1], fftDataVector[rootIndex1+1]);
        
        if (magI/fftMag1 < 10) //if the ratio is less than 20dB, continue. Otherwise don't bother
        {
            fftMagMin1 = hypotf(fftDataVector[rootIndex1-2], fftDataVector[rootIndex1-1]);
            fftMagPlus1 = hypotf(fftDataVector[rootIndex1+2], fftDataVector[rootIndex1+3]);
            
            
            if (fftMag1 > fftMagMin1 && fftMag1 > fftMagPlus1 ) //if it's a local max
            {
                i = rootIndex1;
                magI = fftMag1;
            }
        }
        
    }
    else if ( i/harmonicNumber == 0)
    {
        fftMag1 = hypotf(fftDataVector[0], fftDataVector[1]);
        fftMagPlus1 = hypotf(fftDataVector[2], fftDataVector[3]);
        
        if (fftMag1 > magI && (getSampleRate()/masterFFTLength > 20.f) ) {  i = 0; } //non-audible fundamentals should not be reported
        if (fftMagPlus1 > magI) { i = 2; magI = fftMagPlus1; }
        
    }
    else
    {
        rootIndex1 = i/harmonicNumber; // Floor index. int/int gives the floor value anyway
        rootIndex1 += (rootIndex1 & 1); //If it's even, do nothing. If it's odd, add 1 to make it even (round up always)
        
        rootIndex2 = rootIndex1+2; // Ceiling index.
        
        fftMag1 = hypotf(fftDataVector[rootIndex1], fftDataVector[rootIndex1+1]);
        fftMag2 = hypotf(fftDataVector[rootIndex2], fftDataVector[rootIndex2+1]);
        
        if (fftMag1 > fftMag2 && magI/fftMag1 < 10) //if Mag1 is the bigger of the two and the ratio to root is less than -20dB
        {
            fftMagMin1 = hypotf(fftDataVector[rootIndex1-2], fftDataVector[rootIndex1-1]);
            if (fftMag1 > fftMagMin1)
            {
                i = rootIndex1;
                magI = fftMag1;
                
            }
        }
        else if (magI/fftMag2 < 10) //the "else" already assumes that fftMag2 > fftMag1
        {
            fftMagPlus1 = hypotf(fftDataVector[rootIndex2+2], fftDataVector[rootIndex2+3]);
            if (fftMag2 > fftMagPlus1)
            {
                i = rootIndex2;
                magI = fftMag2;
            }
        }
    } //end else (not divByX)
    
    return i;
}

float SimpleTunerAudioProcessor::findExactMaxFrequency(std::vector<float>& fifoFFTData1, std::vector<float>& fifoFFTData2)
{
    //At this point we already took the FFT. Data1 is the current FFT data, Data2 is the previous FFT data. We need both in order to find the phase remainder of the current FFT data
    
    int maxIndex = findComplexMaxIndex(fifoFFTData1);
    
    
    
    if ( std::hypot(fifoFFTData1[maxIndex], fifoFFTData1[maxIndex+1]) < fftThreshold)
    {
        //If the magnitude @ maxIndex is below the noise threshold
        return 0.0f;
    }
    
    float topPhase = std::atan2f(fifoFFTData1[maxIndex+1], fifoFFTData1[maxIndex]); //atan2(imag, real)
    float nextPhase = std::atan2f(fifoFFTData2[maxIndex+1], fifoFFTData2[maxIndex]);
    
   
    
    float phaseRemainder = (nextPhase-topPhase) - dummyBuffer.getNumSamples()*juce::MathConstants<float>::twoPi*(maxIndex/2)/masterFFTLength;
    phaseRemainder = std::remainder(phaseRemainder, juce::MathConstants<float>::twoPi); //angle wrap from -pi to pi
   
    
    float exactOmega = phaseRemainder/dummyBuffer.getNumSamples() + juce::MathConstants<float>::twoPi*(maxIndex/2)/masterFFTLength;

    
    return ( getSampleRate()*exactOmega/juce::MathConstants<float>::twoPi );
    
}

float SimpleTunerAudioProcessor::getCurrentExactF()
{
    return currentExactF;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleTunerAudioProcessor();
}

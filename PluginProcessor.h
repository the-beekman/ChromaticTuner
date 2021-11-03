/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================

/*
 * MAKING THE FFT: THE BASIC IDEA
 * We are forming a FIFO (circular buffer) of AudioBufferFIFOs.
 * The samples in the buffer that processBlock gets from the DAW is put into a bufferToFill inside AudioBufferFifo
 * Once AudioBufferFIFO's bufferToFill is full, it gets pushed onto a FifoStructure
 * On every call to processBlock, we pull buffers from the AudioBufferFifo and copy them to the end of another buffer. Then we take the FFT of that other buffer.
 * This happens for every complete buffer in the AudioBufferFIFO. If the AudioBufferFIFO is in a state where there is more than 1 complete buffer available, the FFT will be taken multiple times in processBlock. However, most of the time there should only be 1 buffer available
 *
 * The FFTDataGenerator also has its own FifoStructure to hold FFT outputs. We use the 2 top entries to find the exact maximum frequency
 */


// The FIFO/FFT structure/flow heavily borrows from the SimpleEQ project tutorial by MatkatMusic. I iterate upon it by adding the pullTopViewNext function
template<typename BufferType>
class FifoStructure
{
public:
    void prepare(int numChannels, int numSamples)
    {
        static_assert( std::is_same_v<BufferType, juce::AudioBuffer<float>>,
                              "prepare(numChannels, numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for (auto& buffer : bufferArray)
        {
            buffer.setSize(1,             //newNumChannels
                           numSamples,    //newNumSamples
                           false,         //keepExistingContent
                           true,          //clearExtraSpace
                           true);         //avoidReallocating if smaller
            buffer.clear();
        }
        emptyBuffer.setSize(1,             //newNumChannels
                       numSamples,    //newNumSamples
                       false,         //keepExistingContent
                       true,          //clearExtraSpace
                       true);         //avoidReallocating if smaller
        emptyBuffer.clear();
        
    }
    
    void prepare(size_t numElements)
    {
        static_assert( std::is_same_v<BufferType, std::vector<float>>,
                              "prepare(numElements) should only be used when the Fifo is holding std::vector<float>");
                for( auto& buffer : bufferArray )
                {
                    buffer.clear();
                    buffer.resize(numElements, 0);
                }
        
        emptyBuffer.clear();
        emptyBuffer.resize(numElements, 0);
    }
    
    bool push(const BufferType& bufferToInsert)
    {
        auto write = abstractFifoTrackerObject.write(1); //returns a juce::AbstractFifo::ScopedReadWrite<read> object with member vars
                //int startIndex1 = on exit, this will contain the start index in your buffer at which your data should be written
                //int blockSize1 = on exit, this indicates how many items can be written to the block starting at startIndex1
                //int startIndex2 = on exit, this will contain the start index in your buffer at which any data that didn't fit into the first block should be written
                //int blockSize2 = on exit, this indicates how many items can be written to the block starting at startIndex2
        
        if (write.blockSize1 > 0) //If there's space in the fifo for the buffer
        {
            bufferArray[write.startIndex1] = bufferToInsert; //write the buffer to the block
            return true; //the push succeeded
        }
        return false; //the push did not succeed
    }
    
    bool pull(BufferType& bufferToHold)
    {
        auto read = abstractFifoTrackerObject.read(1); //read from the top of the fifo
        if (read.blockSize1 > 0) //if we could read something
        {
            bufferToHold = bufferArray[read.startIndex1];
            return true; //succeeded
        }
        return false; //failed
    }
    
    int pullTopViewNext(BufferType& bufferToHold1, BufferType& bufferToHold2)
    {
        //function returns the number of buffers we were able to see
        int startIndex1, blockSize1, startIndex2, blockSize2;
        
        abstractFifoTrackerObject.prepareToRead(2, startIndex1, blockSize1, startIndex2, blockSize2);
        
        //If there's no blocks available to read, do nothing (return false)
        if (blockSize1 == 0)
        {
            return 0;
        }
        
        //If startIndex1 only has space for 1 block, the other block should be at startIndex2
        else if (blockSize1 == 1)
        {
            bufferToHold1 = bufferArray[startIndex1];
            
            if (blockSize2 > 0)
            {
                bufferToHold2 = bufferArray[startIndex2];
                abstractFifoTrackerObject.finishedRead(1); //Discard startIndex1, keep startIndex2
                return 2;
            }
            
            bufferToHold2 = emptyBuffer;
            
            //Previously we did if there's only 1 buffer available to pull, don't destroy it.
            //That backfired and caused an infinite loop in the FFT while function. So We will see if destroying the single block helps...
            abstractFifoTrackerObject.finishedRead(1);
            return 1;
        }
        
        else if (blockSize1 > 1)
        {
            bufferToHold1 = bufferArray[startIndex1];
            bufferToHold2 = bufferArray[startIndex1+1];
            abstractFifoTrackerObject.finishedRead(1); //Discard buffer1, keep buffer2
            return 2;
        }
        
        else
        {
            return 0;
        }
    }
    
    int getNumAvailableForReading() const {return abstractFifoTrackerObject.getNumReady();}
    
private:
    static constexpr int BufferCapacity = 30;
    std::array<BufferType, BufferCapacity> bufferArray;
    juce::AbstractFifo abstractFifoTrackerObject {BufferCapacity}; //This keeps track of the pointers for a circular buffer structure
    BufferType emptyBuffer;
    
};

template<typename BlockType> //juce::AudioBuffer<float>
class AudioBufferFifo
{
//The SimpleEQ project by MatkatMusic was designed for multi-channel. Here I only support single channel
//In implementation, the bufferSize The bufferSize in this class is the same as the samplesPerBlock that the DAW works with.
public:
    AudioBufferFifo()
    {
        prepared.set(false);
    }

    //This gets called when the DAW buffer size or sample rate changes (when prepareToPlay is called)
    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);
        
        bufferToFill.setSize(1,             //newNumChannels
                             bufferSize,    //newNumSamples
                             false,         //keepExistingContent
                             true,          //clearExtraSpace
                             true);         //avoidReallocating if smaller
        fifoStructure.prepare(1, bufferSize);
        bufferIndex = 0;
        prepared.set(true);
    }
    
    void update(const BlockType& buffer)
    {
        jassert(prepared.get()); //we don't want to use isPrepared() to save 1 function call
        jassert(buffer.getNumChannels() > 0);
        auto* bufferPtr = buffer.getReadPointer(0); //always use channel 0
        
        //go sample-by-sample pushing into the bufferToFill
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            pushNextSampleIntoFifo( bufferPtr[i] );
        }
    }
    
    int getNumCompleteBuffersAvailable() const {return fifoStructure.getNumAvailableForReading();}
    bool isPrepared() const {return prepared.get();}
    int getSize() const {return size.get();}
    bool getAudioBuffer(BlockType& buf) {return fifoStructure.pull(buf);}
    
private:
    juce::Atomic<bool> prepared = false; //Atomic to support multi-threading
    juce::Atomic<int> size = 0; //the size of each buffer
    BlockType bufferToFill; //is practically a juce::AudioBuffer<float>
    FifoStructure<BlockType> fifoStructure;
    int bufferIndex = 0; //keeps track of the position in the 
    
    void pushNextSampleIntoFifo(float sample)
    {
        if ( bufferIndex == bufferToFill.getNumSamples() ) //wraparound
        {
            bool pushSucceeded = fifoStructure.push(bufferToFill); //push the full buffer on to the fifo
            juce::ignoreUnused(pushSucceeded);
            bufferIndex = 0;
        }
        
        bufferToFill.setSample(0, bufferIndex, sample); //ch0, index fifoIndex
        ++bufferIndex;
    }
    
};

template<typename BlockType>
class FFTDataGenerator //using BlockType = std::vector<float>
{
public:
    FFTDataGenerator(int fftOrder)
    {
        order = fftOrder;
        int fftSize = getFFTSize();
        
        fftObject = std::make_unique<juce::dsp::FFT>(order);
        window = std::make_unique<juce::dsp::WindowingFunction<float>>(
            fftSize,
            juce::dsp::WindowingFunction<float>::blackmanHarris //was hann, now blackmanHarris to minimize SLL
            );
        
        fftData.clear();
        fftData.resize(fftSize*2, 0);
        
        fftDataFifo.prepare(fftData.size());
    }
    
    void produceFFTData(const juce::AudioBuffer<float>& audioData)
    {
        //This function takes a full fftSize audio buffer and takes a windowed FFT
        
        const int fftSize = getFFTSize();
        
        fftData.assign(fftData.size(),0); //fftData is a std::vector in the example code. In the constructor fftData.size() should be fftSize*2.
        
        auto* readIndex = audioData.getReadPointer(0);
        
        //now we fill the fftData block with the audio data...
        std::copy(readIndex, readIndex+fftSize, fftData.begin()); //Since readIndex is a pointer to samples (memoryaddress), we copy the memory addresses to the fftData buffer.
            //Only the first half is filled, the other half is all 0
        
        //Apply a windowing function
        window->multiplyWithWindowingTable(fftData.data(),fftSize);
        
        //Then perform the FFT
        fftObject->performRealOnlyForwardTransform(fftData.data());
        
        //At this point the fftData is now even-index=real part, odd-index=imag part
        fftDataFifo.push(fftData);
    }
    
    int pullTopViewNext(BlockType& bufferToHold1, BlockType& bufferToHold2) {return fftDataFifo.pullTopViewNext(bufferToHold1, bufferToHold2);}
    int getFFTSize() const { return 1 << order; }
    int getNumAvailableFFTDataBlocks() const { return fftDataFifo.getNumAvailableForReading();}
    bool getFFTData(BlockType& fftData) {return fftDataFifo.pull(fftData);}
private:
    BlockType fftData; //std::vector<float>
    std::unique_ptr<juce::dsp::FFT> fftObject;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    int order;
    FifoStructure<BlockType> fftDataFifo; //using BlockType = std::vector<float>
};


class SimpleTunerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SimpleTunerAudioProcessor();
    ~SimpleTunerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    //My Variables==================================================================

    template<typename DataType>
    int findComplexMaxIndex(std::vector<DataType>& fftDataVector );
    
    float findExactMaxFrequency(std::vector<float>& fifoFFTData1, std::vector<float>& fifoFFTData2);
    
    float getCurrentExactF();
    
    float wrapToPi(float phi)
    {
        phi = std::fmod(phi + juce::MathConstants<float>::twoPi/2,juce::MathConstants<float>::twoPi);
           if (phi < 0)
               phi += juce::MathConstants<float>::twoPi;
           return phi - juce::MathConstants<float>::twoPi/2;
    }
    
    enum FFTOrder {order2048 = 11, order4096 = 12, order8192 = 13};
    const int masterFFTOrder = FFTOrder::order8192; // was 11 (2048, 23.43Hz res @ 48kHz) now 13 (8192, 5.46Hz res @ 48kHz)
    const int masterFFTLength = 1 << masterFFTOrder;

private:
    //==============================================================================
    
    AudioBufferFifo< juce::AudioBuffer<float> > bufferFifo;
    FFTDataGenerator< std::vector<float> > fftDataStructure {masterFFTOrder};
    
    juce::AudioBuffer<float> dummyBuffer;
    juce::AudioBuffer<float> audioBufferForFFT;
    
    std::complex<float>* topFFTDataComplex;
    std::complex<float>* nextFFTDataComplex;
    
    template<typename DataType>
    int fundamentalFrequencyChecker(int i, float magI, std::vector<DataType>& fftDataVector);
    template<typename DataType>
    int checkSpecificHarmonic(int harmonicNumber, int i, float& magI, std::vector<DataType>& fftDataVector);
    
    std::atomic<float> currentExactF = 0;
    float fftThreshold = 0.001*masterFFTLength; // 0.001 = -60dB

    std::vector<float> topFFTData;
    std::vector<float> nextFFTData;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleTunerAudioProcessor)
    
};



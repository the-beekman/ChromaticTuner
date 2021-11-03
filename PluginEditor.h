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
struct NoteData
{
    NoteData (juce::String c_noteName, float c_cents, bool c_sharp=false)
    {
        noteName = c_noteName;
        cents = c_cents;
        sharp = c_sharp;
    }
    
    NoteData()
    {
        noteName = juce::String("-");
        cents = 0;
    }
    juce::String noteName;
    float cents;
    bool sharp;
};

struct MeterRectangles
{
    std::vector< juce::Rectangle<int> > rectangles;
    std::vector<int> rectangleStatus;
    
    void resize(int newSize)
    {
        rectangles.resize(newSize);
        rectangleStatus.resize(newSize);
        
        for(int i = 0; i < newSize; ++i)
        {
            rectangleStatus.at(i) = 0;
        }
    }
    
    int size() { return static_cast<int>(rectangles.size());}
};

struct MeterTriangles
{
    std::vector< juce::Path > triangles;
    
    MeterTriangles()
    {
        triangles.resize(3);
    }
};

class SimpleTunerAudioProcessorEditor  : public juce::AudioProcessorEditor, juce::Timer
{
public:
    SimpleTunerAudioProcessorEditor (SimpleTunerAudioProcessor&);
    ~SimpleTunerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    
    
    NoteData convertFreqToString(float freq, float refA4);
    


private:
    void customizeLookAndFeel();
    
    const int refreshRate = 12; //was 60fps. Now 12 to reduce flashing lights
    float centTolerance = 1;
    
    juce::String tunerDisplay {"Welcome!"};
    
    //For the DSP
    NoteData noteData;
    SimpleTunerAudioProcessor& audioProcessor;
    
    float m_currentExactF {0}, m_previousExactF {0}, m_freqToDisplay {0};
    
    void updateNoteData();
    
    //For the GUI
    float triangleHeightPercent = 0.25;

    float noteAreaHeightPercent = 0.5;
    
    const int numMeterRectsPerSide = 5;
    
    MeterRectangles meterRectangles;

    int  meterRectSpacing, meterRectWidth, meterRectHeight;
    float meterRectSpacingScalar = 0.5;
    float meterRectPaddingScalar = 0.1;
    float meterRectHeightRatio = 1.618;
    
    void setMeterRectangleStatus(float cents);
    void resetMeterRectangleStatus();
    void drawMeterRectangles(juce::Graphics& g);
    void initializeMeterRectangles();
    
    juce::Rectangle<int> triangleArea, rectangleArea, noteArea, buttonArea;
    
    MeterTriangles meterTriangles;
    int meterTriPaddingFromTopPixels = 20;
    float meterTriDistanceFromCenterScalar = 0.5;
    int triangleBase, triangleHeight;
    void drawTriangles(juce::Graphics& g);
    void initializeMeterTriangles();
    
    
    juce::GlyphArrangement noteTextArrangement, refTextArrangement; //basically text fields
    juce::Rectangle<float> noteLetterBoundingBox, refTextBoundingBox;
    void drawNote(juce::Graphics& g);
    const int noteDistanceFromRectangles = 0;
    
    enum MeterMode{
        Chromatic,
        Strobe
    };
    int meterMode = MeterMode::Chromatic;
    int strobeFrameCounter;
    
    float referenceFrequency = 440;
    void drawReferenceText(juce::Graphics& g);
    
    juce::TextButton chromaticButton, strobeButton;
    void initializeModeButtons();
    int modeButtonWidth = 75; //pixels
    int modeButtonPaddingX = 10; //pixels
    int modeButtonHeight = 25; //pixels
    int modeButtonPaddingY = 10; //pixels
    
    juce::TextButton refPlusButton, refMinusButton;
    void initializeRefButtons();
    float getReferenceTextWidth();
   
    int refButtonWidth; //Placeholder. value set in initializeRefButtons()
    int refTextWidth; //Placeholder. value set in initializeRefButtons()
    
    
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR  (SimpleTunerAudioProcessorEditor)
};

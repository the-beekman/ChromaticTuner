/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleTunerAudioProcessorEditor::SimpleTunerAudioProcessorEditor (SimpleTunerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    
    customizeLookAndFeel();
    
    addAndMakeVisible(chromaticButton);
    addAndMakeVisible(strobeButton);
    addAndMakeVisible(refPlusButton);
    addAndMakeVisible(refMinusButton);
    
    meterRectangles.resize(2*numMeterRectsPerSide+1);
    
    startTimerHz(refreshRate); //from juce::Timer, this is why we inherited that
    
    setSize (400, 300); //setSize calls resized, so it should be the last thing...
}

SimpleTunerAudioProcessorEditor::~SimpleTunerAudioProcessorEditor()
{
}

//==============================================================================
void SimpleTunerAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        
    
    drawNote(g);
    
    drawMeterRectangles(g);
    drawTriangles(g);
    drawReferenceText(g);
}

void SimpleTunerAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    juce::Rectangle<int> bounds = getLocalBounds();
    const int boundsOriginalHeight = bounds.getHeight();
    const int boundsOriginalWidth = bounds.getWidth();
    
    meterRectWidth = boundsOriginalWidth*(1-2*meterRectPaddingScalar)/(2*numMeterRectsPerSide+1 + 10*meterRectSpacingScalar);
    meterRectSpacing = meterRectSpacingScalar*meterRectWidth;
    
     triangleArea = bounds.removeFromTop(static_cast<int>(boundsOriginalHeight*triangleHeightPercent));
     rectangleArea = bounds.removeFromTop(static_cast<int>(meterRectWidth*meterRectHeightRatio));
     noteArea = bounds.removeFromTop(static_cast<int>(boundsOriginalHeight*noteAreaHeightPercent));
    buttonArea = bounds;
    
    initializeMeterRectangles();
    initializeMeterTriangles();
    initializeModeButtons();
    initializeRefButtons();
}

void SimpleTunerAudioProcessorEditor::customizeLookAndFeel()
{
    getLookAndFeel().setColour(juce::ResizableWindow::backgroundColourId, juce::Colour {62,62,62} );
    
    getLookAndFeel().setDefaultSansSerifTypefaceName("Arial");
}

void SimpleTunerAudioProcessorEditor::timerCallback()
{
    updateNoteData();
    tunerDisplay = noteData.noteName;
    repaint();
}

void SimpleTunerAudioProcessorEditor::updateNoteData()
{
    //m_previousExactF = m_currentExactF; //if we wanted to average the exactF to smooth it out a bit
    m_currentExactF = audioProcessor.getCurrentExactF();
    
    noteData = convertFreqToString(m_currentExactF, referenceFrequency);
}


NoteData SimpleTunerAudioProcessorEditor::convertFreqToString(float freq, float refA4)
{
    if (freq == 0.0f) {return NoteData(juce::String(""), 0);}
    float logRatio = std::log10( freq/refA4 )/std::log10(2);    //All of the equations are based off the log2 of freq/ref.
                                                                //logRatio is also the number of octaves from A4 (reference)

    float numSemitonesFromRef = 12*logRatio;                    //octaves -> semitones
    int numSemitonesFromRef_round = roundf(numSemitonesFromRef);
    float numCentsFromNote = 100*(numSemitonesFromRef-numSemitonesFromRef_round); //1 cent = 1/100 of a semitone
    
    //DBG("currentExactF = " << freq << " numCentsFromNote : " << numCentsFromNote);
    
    juce::String noteName;
    bool sharp = false;
    
    switch ( numSemitonesFromRef_round % 12 )
    {

        case 0: {noteName = juce::String("A"); break;}
        
        //A#
        case -11:
        case 1: {noteName = juce::String("A"); sharp=true; break;}

        case -10:
        case 2: {noteName = juce::String("B"); break;}

        case -9:
        case 3: {noteName = juce::String("C"); break;}
        
        //C#
        case -8:
        case 4: {noteName = juce::String("C"); sharp=true; break;}

        case -7:
        case 5: {noteName = juce::String("D"); break;}
            
        //D#
        case -6:
        case 6: {noteName = juce::String("D"); sharp=true; break;}
            
        case -5:
        case 7: {noteName = juce::String("E"); break;}
            
        case -4:
        case 8: {noteName = juce::String("F"); break;}
        
        //F#
        case -3:
        case 9: {noteName = juce::String("F"); sharp=true; break;}
            
        case -2:
        case 10: {noteName = juce::String("G"); break;}
            
        //G#
        case -1:
        case 11: {noteName = juce::String("G"); sharp=true; break;}
            
        default: {DBG(numSemitonesFromRef_round % 12); noteName = juce::String("#"); break;}
    }
    return NoteData(noteName, numCentsFromNote, sharp);
}

void SimpleTunerAudioProcessorEditor::drawMeterRectangles(juce::Graphics& g)
{
    for (int i = 0; i <  meterRectangles.size(); ++i)
    {
        juce::Rectangle<int>& rect = meterRectangles.rectangles.at(i);
        if (i == 5)
        {
            if (meterMode == MeterMode::Chromatic)
            {
            (meterRectangles.rectangleStatus.at(i)) ?  g.setColour(juce::Colours::lightgreen) : g.setColour(juce::Colours::darkgreen);
            }
            else if (meterMode == MeterMode::Strobe)
            {
                (meterRectangles.rectangleStatus.at(i)) ?  g.setColour(juce::Colours::red) : g.setColour(juce::Colours::darkred);
            }
        }
        else
        {
            (meterRectangles.rectangleStatus.at(i)) ?  g.setColour(juce::Colours::red) : g.setColour(juce::Colours::darkred);
        }
        g.drawRect(rect);
        g.fillRect(rect);
    }
}

void SimpleTunerAudioProcessorEditor::setMeterRectangleStatus(float cents)
{
    //THIS SECTION RELIES ON NUMMETERRECTSPERSIDE == 5
    if (meterMode == MeterMode::Chromatic)
    {
        //clear everything
        for (int& status : meterRectangles.rectangleStatus) {status = 0;}
        
        if (cents <= centTolerance && cents >= -centTolerance) { meterRectangles.rectangleStatus.at(5) = 1; }
        else if (cents > 0)
        {
            if (cents >= 40.f) {meterRectangles.rectangleStatus.at(10) = 1;}
            else if (cents >= 30.f) {meterRectangles.rectangleStatus.at(9) = 1;}
            else if (cents >= 20.f) {meterRectangles.rectangleStatus.at(8) = 1;}
            else if (cents >= 10.f) {meterRectangles.rectangleStatus.at(7) = 1;}
            else if (cents >= centTolerance) {meterRectangles.rectangleStatus.at(6) = 1;}
        }
        else if (cents < 0){
            if (cents <= -40.f) {meterRectangles.rectangleStatus.at(0) = 1;}
            else if (cents <= -30.f) {meterRectangles.rectangleStatus.at(1) = 1;}
            else if (cents <= -20.f) {meterRectangles.rectangleStatus.at(2) = 1;}
            else if (cents <= -10.f) {meterRectangles.rectangleStatus.at(3) = 1;}
            else if (cents <= -centTolerance) {meterRectangles.rectangleStatus.at(4) = 1;}
        }
    }
    else if (meterMode == MeterMode::Strobe)
    {
        float rotationsPerSecond = cents/10;    //50 -> 5 (0.2s), 10 -> 1 (1s)
                                                //Could do better with a non-linear function?
        int numFramesUntilAdvance = static_cast<int>(refreshRate/rotationsPerSecond);
        if (strobeFrameCounter % numFramesUntilAdvance == 0)
        {
            
            //shift everything by 1
            if (cents < -centTolerance)
            {
                //rotate counterclockwise (right
                std::rotate(meterRectangles.rectangleStatus.rbegin(), meterRectangles.rectangleStatus.rbegin()+1, meterRectangles.rectangleStatus.rend()); //orig_first, new_first, orig_last
            }
            else if (cents > centTolerance)
            {
                //rotate clockwise (left)
                std::rotate(meterRectangles.rectangleStatus.begin(), meterRectangles.rectangleStatus.begin()+1, meterRectangles.rectangleStatus.end()); //orig_first, new_first, orig_last
            }
        }
        
        strobeFrameCounter = ++strobeFrameCounter % numFramesUntilAdvance;
    }
}

void SimpleTunerAudioProcessorEditor::resetMeterRectangleStatus()
{
    for (int& status : meterRectangles.rectangleStatus) {status = 0;}
        
    if (meterMode == MeterMode::Strobe)
    {
        meterRectangles.rectangleStatus.at(4) = 1;
        meterRectangles.rectangleStatus.at(6) = 1;
    }
    strobeFrameCounter = 0;
}

void SimpleTunerAudioProcessorEditor::drawTriangles(juce::Graphics& g)
{
    auto f_cents = noteData.cents;
    
    
    //Flat triangle
    (f_cents < -centTolerance) ? g.setColour(juce::Colours::red) : g.setColour(juce::Colours::darkred);
    g.fillPath(meterTriangles.triangles.at(0));
    
    //Intune triangle
    (f_cents < centTolerance && f_cents > -centTolerance && noteData.noteName != juce::String("")) ? g.setColour(juce::Colours::lightgreen) : g.setColour(juce::Colours::darkgreen);
    g.fillPath(meterTriangles.triangles.at(1));
    
    //sharp triangle
    (f_cents > centTolerance) ? g.setColour(juce::Colours::red) : g.setColour(juce::Colours::darkred);
    g.fillPath(meterTriangles.triangles.at(2));
    
}

void SimpleTunerAudioProcessorEditor::initializeMeterRectangles()
{
    int xPosition = meterRectPaddingScalar*rectangleArea.getWidth();
    int yPosition = rectangleArea.getY()*(1+0);
    
    //DBG("Entered initializeMeterRectangles()");
    
    for (juce::Rectangle<int>& rect : meterRectangles.rectangles)
    {
        //Here, the components are getting made (resize works) but the things don't keep their values afterwards
        //Fixed by changing the for loop to reference
        
        
        rect = juce::Rectangle<int>(xPosition, yPosition, meterRectWidth, meterRectWidth*meterRectHeightRatio); //final arg is height.
        xPosition += meterRectWidth+meterRectSpacing;
    }
    
    resetMeterRectangleStatus();
}

void SimpleTunerAudioProcessorEditor::initializeMeterTriangles()
{
    //Look at juce::Rectangle triangleArea
    
    for (juce::Path& path : meterTriangles.triangles)
    {
        path.clear();
    }
    
    triangleBase = 20;
    triangleHeight = 20;
    
    float areaCenterX = triangleArea.getX() + (triangleArea.getWidth() / 2.f);
    int triangleTopY = triangleArea.getY()+meterTriPaddingFromTopPixels;
    
    float leftEdge = areaCenterX*meterTriDistanceFromCenterScalar-(triangleHeight/2.f);
    float rightEdge = areaCenterX*(1+meterTriDistanceFromCenterScalar)+(triangleHeight/2.f);
    
    meterTriangles.triangles.at(0).addTriangle(leftEdge, triangleTopY, leftEdge, triangleTopY+triangleBase, leftEdge+triangleHeight, triangleTopY+triangleHeight/2.f ); //Left (Flat)
    
    meterTriangles.triangles.at(1).addTriangle(areaCenterX-triangleBase/2.f, triangleTopY, areaCenterX+triangleBase/2, triangleTopY, areaCenterX, triangleTopY+triangleHeight); //Center (In-tune)
    
    meterTriangles.triangles.at(2).addTriangle(rightEdge, triangleTopY, rightEdge, triangleTopY+triangleBase, rightEdge-triangleBase, triangleTopY+triangleHeight/2.f); //Right (Sharp)
}

void SimpleTunerAudioProcessorEditor::initializeModeButtons()
{
    chromaticButton.setButtonText(std::string("Note"));
    strobeButton.setButtonText(std::string("Strobe"));
    
    int areaBottom = buttonArea.getBottom();
    int rightEdge = buttonArea.getRight();
    
    modeButtonWidth = strobeButton.getBestWidthForHeight(modeButtonHeight);
        
    chromaticButton.setBounds(rightEdge-(2*modeButtonWidth+2*modeButtonPaddingX), areaBottom-(modeButtonHeight+modeButtonPaddingY), modeButtonWidth, modeButtonHeight);
    strobeButton.setBounds(rightEdge-(modeButtonWidth+modeButtonPaddingX), areaBottom-(modeButtonHeight+modeButtonPaddingY), modeButtonWidth, modeButtonHeight);
    
    chromaticButton.setTooltip("Switch to chromatic mode.");
    strobeButton.setTooltip("Switch to strobe mode.");
    
    //We don't hard-code it because initialize gets called on resize and we want to keep the state
    chromaticButton.setToggleState(meterMode == MeterMode::Chromatic, juce::NotificationType::dontSendNotification); //Default to chromatic mode. 2nd arg is NotificationType notification
    strobeButton.setToggleState(meterMode == MeterMode::Strobe, juce::NotificationType::dontSendNotification);

    chromaticButton.onClick = [&]()
    {
        //we passed the capture by reference so our changes will be preserved outside the function
        strobeButton.setToggleState(false, juce::NotificationType::dontSendNotification);
        chromaticButton.setToggleState(true, juce::NotificationType::dontSendNotification);
        meterMode = MeterMode::Chromatic;
        resetMeterRectangleStatus();
    };
    
    strobeButton.onClick = [&]()
    {
        //we passed the capture by reference so our changes will be preserved outside the function
        strobeButton.setToggleState(true, juce::NotificationType::dontSendNotification);
        chromaticButton.setToggleState(false, juce::NotificationType::dontSendNotification);
        meterMode = MeterMode::Strobe;
        resetMeterRectangleStatus();
    };
    
}

void SimpleTunerAudioProcessorEditor::initializeRefButtons()
{
    refMinusButton.setButtonText(std::string("-"));
    refPlusButton.setButtonText(std::string("+"));
    
    int areaBottom = buttonArea.getBottom();
    int leftEdge = buttonArea.getX();
    
    refButtonWidth = refMinusButton.getBestWidthForHeight(modeButtonHeight);
        
    refMinusButton.setBounds(leftEdge+modeButtonPaddingX, areaBottom-(modeButtonHeight+modeButtonPaddingY), refButtonWidth, modeButtonHeight);
    
    
    refTextWidth = static_cast<int>(getReferenceTextWidth());
    int refPlusButtonXPosition = leftEdge+refButtonWidth+3*modeButtonPaddingX+refTextWidth;
    
    refPlusButton.setBounds(refPlusButtonXPosition, areaBottom-(modeButtonHeight+modeButtonPaddingY), refButtonWidth, modeButtonHeight);
    
    refMinusButton.setTooltip("Subtract 1 from reference frequency.");
    refPlusButton.setTooltip("Add 1 to reference frequency.");
    
    refMinusButton.onClick = [&]()
    {
        //we passed the capture by reference so our changes will be preserved outside the function
        if (referenceFrequency > 430.f)
        {
        --referenceFrequency;
        }
    };
    
    refPlusButton.onClick = [&]()
    {
        if (referenceFrequency < 450.f)
        {
        ++referenceFrequency;
        }
    };
}

float SimpleTunerAudioProcessorEditor::getReferenceTextWidth(/*juce::Graphics& g*/)
{
    float fontHeightArg = ( (float)strobeButton.getHeight()*0.6 > 14.f) ? 14.f : (float)strobeButton.getHeight()*0.6;
    
    const int fontHeight = juce::roundToInt(fontHeightArg);
    
    juce::String refText = juce::String("A: ")+juce::String(referenceFrequency, 0)+juce::String("Hz");
    juce::Font displayFont = juce::Font(fontHeight,juce::Font::plain);

    refTextArrangement.clear();
    refTextArrangement.addLineOfText(displayFont,
                                     refText,
                                     buttonArea.getX()+refButtonWidth+2*modeButtonPaddingX,
                                     strobeButton.getY());
    
    refTextBoundingBox = refTextArrangement.getBoundingBox(0, -1, true); //startIndex, numGlyphs, includeWhitespace
    

    
    return refTextBoundingBox.getWidth();
}

void SimpleTunerAudioProcessorEditor::drawReferenceText(juce::Graphics& g)
{
    juce::Font displayFont = juce::Font(buttonArea.getHeight(),juce::Font::plain);
    g.setColour (juce::Colours::white);

    
        
    juce::String refText = juce::String("A: ")+juce::String(referenceFrequency, 0)+juce::String("Hz");
    
    g.drawText(refText,
               buttonArea.getX()+refButtonWidth+2*modeButtonPaddingX, //X (topleft)
               strobeButton.getY(), //Y (top left)
               refTextWidth, //maximum width
               strobeButton.getHeight(), //height (downward)
               juce::Justification::centredLeft);
    
}

void SimpleTunerAudioProcessorEditor::drawNote(juce::Graphics& g)
{
    g.setColour (juce::Colours::white);
    juce::Font displayFont = juce::Font(noteArea.getHeight(),juce::Font::bold);

    tunerDisplay = noteData.noteName;
    
    int textBaseline = noteArea.getY()+noteDistanceFromRectangles+displayFont.getAscent(); //The ascent is the distance from the top to bottom of capital A

    noteTextArrangement.clear();
    noteTextArrangement.addJustifiedText(displayFont, tunerDisplay, noteArea.getX(), textBaseline, noteArea.getWidth(), juce::Justification::horizontallyCentred);
    
    noteLetterBoundingBox = noteTextArrangement.getBoundingBox(0, 1, false); //startIndex, numGlyphs, includeWhitespace
    
    if (noteData.sharp)
    {
        juce::Font sharpFont = juce::Font(noteArea.getHeight()/2,juce::Font::plain);
        int sharpXPos = noteLetterBoundingBox.getRight()+displayFont.getExtraKerningFactor();
        int sharpYPos = noteLetterBoundingBox.getCentreY();
        noteTextArrangement.addLineOfText(sharpFont, "#", sharpXPos, sharpYPos);
    }
    
    
    if (tunerDisplay != juce::String(""))
    {
        setMeterRectangleStatus(noteData.cents);
    }
    else
    {
        resetMeterRectangleStatus();
    }
    
    noteTextArrangement.draw(g);
    
}

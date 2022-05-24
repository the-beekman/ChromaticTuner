# ChromaticTuner
A chromatic tuner made using the JUCE framework in C++(17). Creates AU and VST3 plug-ins for Mac (Intel) and Windows (x64). 

Algorithm finds the maximum frequency bin of the FFT of the signal, then uses the difference of the phase component at that maximum bin compared to the previous FFT to calculate the exact frequency of the signal. The signal is "in-tune" when it has an error of less than 1 cent.

Includes support for reference frequencies from A=430Hz to A=450Hz and meter & strobe display modes.

https://user-images.githubusercontent.com/88636127/139801197-a4c622a7-42f1-4997-b2b5-5b073d95f262.mov


#include <juce_gui_extra/juce_gui_extra.h>
#include "app/MainWindow.h"
class JeffDawApplication final:public juce::JUCEApplication{public:const juce::String getApplicationName()override{return"Jeff DAW Studio";}const juce::String getApplicationVersion()override{return"0.1.0";}void initialise(const juce::String&)override{window=std::make_unique<jeff::daw::MainWindow>();}void shutdown()override{window.reset();}private:std::unique_ptr<jeff::daw::MainWindow>window;};START_JUCE_APPLICATION(JeffDawApplication)

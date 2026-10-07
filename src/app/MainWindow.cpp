#include "app/MainWindow.h"
#include "app/RootComponent.h"
namespace jeff::daw {MainWindow::MainWindow():DocumentWindow("Jeff DAW Studio",juce::Colour(0xff101319),allButtons){setUsingNativeTitleBar(true);setContentOwned(new RootComponent(),true);setResizable(true,true);setResizeLimits(960,640,4096,2160);centreWithSize(1280,800);setVisible(true);}void MainWindow::closeButtonPressed(){juce::JUCEApplication::getInstance()->systemRequestedQuit();}}

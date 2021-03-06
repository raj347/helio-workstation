/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "MainWindow.h"
#include "MainLayout.h"
#include "MidiEditor.h"
#include "Config.h"
#include "SerializationKeys.h"
#include "FatalErrorScreen.h"
#include "HelioTheme.h"
#include "BinaryData.h"
#include "ThemeSettings.h"
#include "ColourSchemeManager.h"
#include "App.h"

class WorkspaceAndroidProxy : public Component
{
public:

	explicit WorkspaceAndroidProxy(MainLayout *target, int targetScale = 2) :
		workspace(target),
		scale(targetScale)
	{
		Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;

		//const double dpi = Desktop::getInstance().getDisplays().getMainDisplay().dpi;
        //const double cmWidth = (screenArea.getWidth() / dpi) * 2.54;
        //const double cmHeight = (screenArea.getHeight() / dpi) * 2.54;

		const float realWidth = float(screenArea.getWidth()) / float(this->scale);
		const float realHeight = float(screenArea.getHeight()) / float(this->scale);

		const float realScaleWidth = float(screenArea.getWidth()) / realWidth;
		const float realScaleheight = float(screenArea.getHeight()) / realHeight;

		AffineTransform newTransform = this->workspace->getTransform();
		newTransform = newTransform.scale(realScaleWidth, realScaleheight);
		this->workspace->setTransform(newTransform);

		this->addAndMakeVisible(this->workspace);
	}

	void resized() override
	{
		if (this->workspace != nullptr)
		{
			this->workspace->setBounds(this->getLocalBounds() / this->scale);
		}
	}

private:

	SafePointer<MainLayout> workspace;

	int scale;

};




MainWindow::MainWindow() :
DocumentWindow("Helio",
               Colours::black,
               DocumentWindow::allButtons)
{
    this->setWantsKeyboardFocus(false);

#if HELIO_DESKTOP

    //this->setResizeLimits(568, 320, 8192, 8192); // phone size test
    this->setResizeLimits(1024, 650, 8192, 8192); // production

#if HELIO_HAS_CUSTOM_TITLEBAR
    this->setResizable(true, true);
    this->setUsingNativeTitleBar(false);
#else
    this->setResizable(true, false);
    this->setUsingNativeTitleBar(true);
#endif

    this->setBounds(int(0.1f * this->getParentWidth()),
                    int(0.1f * this->getParentHeight()),
                    jmin(1280, int(0.85f * this->getParentWidth())),
                    jmin(768, int(0.85f * this->getParentHeight())));
#endif

#if JUCE_IOS
    this->setVisible(false);
#endif

#if JUCE_ANDROID
    this->setFullScreen(true);
    Desktop::getInstance().setKioskModeComponent(this);
#endif

    // HelioTheme have been set previously in App init procedure
    if (HelioTheme *ht = dynamic_cast<HelioTheme *>(&this->getLookAndFeel()))
    {
        ht->initColours(ColourSchemeManager::getInstance().getCurrentScheme());
    }

    const String openGLState = Config::get(Serialization::Core::openGLState);

#if JUCE_MAC
	const bool shouldEnableOpenGLByDefault = openGLState.isEmpty();
#else
	const bool shouldEnableOpenGLByDefault = false;
#endif

    if ((openGLState == Serialization::Core::enabledState) || shouldEnableOpenGLByDefault)
    {
        this->setOpenGLRendererEnabled(true);
    }

    this->createWorkspaceComponent();

    //if (App::isRunningOnPhone())
    //{
    //    this->setContentOwned(new FatalErrorScreen(), false);
    //}

    this->setVisible(true);

#if JUCE_IOS
    Desktop::getInstance().setKioskModeComponent(this);
#endif
}


MainWindow::~MainWindow()
{
    if (this->isOpenGLRendererEnabled())
    {
        this->detachOpenGLContext();
    }
    
    this->dismissWorkspaceComponent();
}

#if HELIO_HAS_CUSTOM_TITLEBAR
BorderSize<int> MainWindow::getBorderThickness()
{
    return BorderSize<int>(0);
}
#endif

#if HELIO_MOBILE
static bool isRunningOnReallySmallScreen()
{
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;
    const double retinaFactor = Desktop::getInstance().getDisplays().getMainDisplay().scale;
    const double dpi = Desktop::getInstance().getDisplays().getMainDisplay().dpi;
    const double cmWidth = (screenArea.getWidth() / dpi) * retinaFactor * 2.54;
    const double cmHeight = (screenArea.getHeight() / dpi) * retinaFactor * 2.54;

    return (cmWidth < 12.0 || cmHeight < 7.0);
}
#endif

bool MainWindow::isRunningOnPhone()
{
#if HELIO_MOBILE
    return isRunningOnReallySmallScreen();
#elif HELIO_DESKTOP
    return false;
#endif
}

bool MainWindow::isRunningOnTablet()
{
#if HELIO_MOBILE
    return !isRunningOnReallySmallScreen();
#elif HELIO_DESKTOP
    return false;
#endif
}

bool MainWindow::isRunningOnDesktop()
{
#if HELIO_MOBILE
    return isRunningOnReallySmallScreen();
#elif HELIO_DESKTOP
    return false;
#endif

}


void MainWindow::closeButtonPressed()
{
    JUCEApplication::getInstance()->systemRequestedQuit();
}

MainLayout *MainWindow::getWorkspaceComponent() const
{
    return this->workspace;
}

void MainWindow::dismissWorkspaceComponent()
{
    this->clearContentComponent();
    this->workspace = nullptr;
}

void MainWindow::createWorkspaceComponent()
{
    this->workspace = new MainLayout();

#if JUCE_ANDROID
    const bool hasRetina = (dpi > 200);

    if (hasRetina)
    {
        this->setContentOwned(new WorkspaceAndroidProxy(this->workspace), false);
    }
    else
    {
        this->setContentNonOwned(this->workspace, false);
    }
#else
    this->setContentNonOwned(this->workspace, false);
#endif

    this->workspace->forceRestoreLastOpenedPage();
}

static ScopedPointer<OpenGLContext> kOpenGLContext = nullptr;

void MainWindow::setOpenGLRendererEnabled(bool shouldBeEnabled)
{
    if (shouldBeEnabled && (kOpenGLContext == nullptr))
    {
        this->attachOpenGLContext();
        Config::set(Serialization::Core::openGLState, Serialization::Core::enabledState);
    }
    else if (!shouldBeEnabled && (kOpenGLContext != nullptr))
    {
        this->detachOpenGLContext();
        Config::set(Serialization::Core::openGLState, Serialization::Core::disabledState);
    }
}

void MainWindow::attachOpenGLContext()
{
    Logger::writeToLog("Attaching OpenGL context.");
    kOpenGLContext = new OpenGLContext();
    kOpenGLContext->setPixelFormat(OpenGLPixelFormat(8, 8, 0, 0));
    kOpenGLContext->setMultisamplingEnabled(false);
    kOpenGLContext->attachTo(*this);
}

void MainWindow::detachOpenGLContext()
{
    Logger::writeToLog("Detaching OpenGL context.");
    kOpenGLContext->detach();
    kOpenGLContext = nullptr;
}

bool MainWindow::isOpenGLRendererEnabled() noexcept
{
    return (kOpenGLContext != nullptr);
}


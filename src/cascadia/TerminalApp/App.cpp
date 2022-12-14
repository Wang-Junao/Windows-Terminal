// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "App.h"
#include "App.g.cpp"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Navigation;

namespace xaml = ::winrt::Windows::UI::Xaml;

namespace winrt::TerminalApp::implementation
{
    App::App()
    {
        Initialize();

        // Disable XAML's automatic backplating of text when in High Contrast
        // mode: we want full control of and responsibility for the foreground
        // and background colors that we draw in XAML.
        HighContrastAdjustment(::winrt::Windows::UI::Xaml::ApplicationHighContrastAdjustment::None);
    }

    void App::Initialize()
    {
        const auto dispatcherQueue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
        if (!dispatcherQueue)
        {
            _windowsXamlManager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        }
        else
        {
            _isUwp = true;
        }
    }

    AppLogic App::Logic()
    {
        static AppLogic logic;
        return logic;
    }

    void App::Close()
    {
        if (_bIsClosed)
        {
            return;
        }

        _bIsClosed = true;

        if (_windowsXamlManager)
        {
            _windowsXamlManager.Close();
        }
        _windowsXamlManager = nullptr;

        Exit();
        {
            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                ::DispatchMessageW(&msg);
            }
        }
    }

    /// <summary>
    /// Invoked when the application is launched normally by the end user.  Other entry points
    /// will be used such as when the application is launched to open a specific file.
    /// </summary>
    /// <param name="e">Details about the launch request and process.</param>
    void App::OnLaunched(const LaunchActivatedEventArgs& /*e*/)
    {
        // if this is a UWP... it means its our problem to hook up the content to the window here.
        if (_isUwp)
        {
            auto content = Window::Current().Content();
            if (content == nullptr)
            {
                auto logic = Logic();
                logic.RunAsUwp(); // Must set UWP status first, settings might change based on it.
                logic.LoadSettings();
                logic.Create();

                auto page = logic.GetRoot().as<TerminalPage>();

                Window::Current().Content(page);
                Window::Current().Activate();
            }
        }
    }
}

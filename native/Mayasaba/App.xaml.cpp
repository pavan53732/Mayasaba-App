#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
namespace winrt::Mayasaba::implementation
{
    App::App()
    {
        InitializeComponent();
    }
    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        window = winrt::make<MainWindow>();
        window.Activate();
    }
}

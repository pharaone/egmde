/*
 * Copyright © 2016-2019 Octopull Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "egwallpaper.h"
#include "egwindowmanager.h"
#include "egshellcommands.h"
#include "eglauncher.h"

#include <miral/append_event_filter.h>
#include <miral/command_line_option.h>
#include <miral/display_configuration_option.h>
#include <miral/internal_client.h>
#include <miral/keymap.h>
#include <miral/runner.h>
#include <miral/set_window_management_policy.h>
#include <miral/version.h>
#include <miral/wayland_extensions.h>
#include <miral/x11_support.h>
#include <mir/fatal.h>

#include <boost/filesystem.hpp>
#include <linux/input.h>

using namespace miral;

int main(int argc, char const* argv[])
{
    MirRunner runner{argc, argv};

    egmde::Wallpaper wallpaper;

    ExternalClientLauncher external_client_launcher;
    egmde::Launcher launcher{external_client_launcher};

    std::set<pid_t> shell_component_pids;

    auto run_apps = [&](std::string const& apps)
    {
        for (auto i = begin(apps); i != end(apps); )
        {
            auto const j = find(i, end(apps), ':');
            shell_component_pids.insert(launcher.run_app(std::string{i, j}, egmde::Launcher::Mode::wayland));
            if ((i = j) != end(apps)) ++i;
        }
    };

    // Protocols we're reserving for shell components
    std::set<std::string> const shell_protocols{
        WaylandExtensions::zwlr_layer_shell_v1,
        WaylandExtensions::zxdg_output_manager_v1,
        WaylandExtensions::zwlr_foreign_toplevel_manager_v1};


    WaylandExtensions extensions;

    for (auto const& protocol : shell_protocols)
    {
        extensions.enable(protocol);
    }

    extensions.set_filter([&](Application const& app, char const* protocol)
        {
            if (shell_protocols.find(protocol) == end(shell_protocols))
                return true;

            return shell_component_pids.find(pid_of(app)) != end(shell_component_pids);
        });

    auto const terminal_cmd = std::string{argv[0]} + "-terminal";

    egmde::ShellCommands commands{runner, launcher, terminal_cmd};

    runner.add_stop_callback([&] { wallpaper.stop(); });
    runner.add_stop_callback([&] { launcher.stop(); });

    int no_of_workspaces = 1;
    auto const update_workspaces = [&](int option)
        {
            // clamp no_of_workspaces to [1..32]
            no_of_workspaces = std::min(std::max(option, 1), 32);
        };

    return runner.run_with(
        {
            X11Support{},
            extensions,
            display_configuration_options,
            CommandLineOption{[&](auto& option) { wallpaper.top(option);},
                              "wallpaper-top",    "Colour of wallpaper RGB", "0x000000"},
            CommandLineOption{[&](auto& option) { wallpaper.bottom(option);},
                              "wallpaper-bottom", "Colour of wallpaper RGB", EGMDE_WALLPAPER_BOTTOM},
            pre_init(CommandLineOption{update_workspaces,
                              "no-of-workspaces", "Number of workspaces [1..32]", no_of_workspaces}),
            CommandLineOption{run_apps, "shell-components", "Colon separated shell components to launch on startup", ""},
            CommandLineOption{[&](bool autostart){ if (autostart) launcher.autostart_apps(); },
                              "shell-enable-autostart", "Autostart apps during startup"},
            StartupInternalClient{std::ref(wallpaper)},
            external_client_launcher,
            StartupInternalClient{std::ref(launcher)},
            Keymap{},
            AppendEventFilter{[&](MirEvent const* e) { return commands.input_event(e); }},
            set_window_management_policy<egmde::WindowManagerPolicy>(wallpaper, commands, no_of_workspaces)
        });
}

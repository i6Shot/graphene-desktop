# This file is part of graphene-desktop, the desktop environment of VeltOS.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
#  
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# settings.py/.plugin
# Plugin loaded by GraphenePanel to add a system settings widget. This is a built-in plugin,
# and is automatically put on the far right side of the panel (but before the clock).

import gi, subprocess
gi.require_version('AccountsService', '1.0')
from gi.repository import GLib, GObject, Gio, Gtk, Gdk, GdkX11, GdkPixbuf, Graphene, AccountsService
import threading, time, subprocess
from battery import GrapheneBatteryIcon, GrapheneBatteryInfo
from volume import GrapheneVolumeIcon, GrapheneVolumeSlider
from network import GrapheneNetworkIcon
import users

class GrapheneSettingsExtension(GObject.Object, Graphene.AppletExtension):
    __gtype_name__ = 'GrapheneSettingsExtension'

    def do_get_widget(self, panel):
        return GrapheneSettingsApplet(panel)


class GrapheneSettingsApplet(Gtk.Button):
    __gtype_name__ = 'GrapheneSettingsApplet'

    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        self.popup = GrapheneSettingsPopup(panel)
        self.get_style_context().add_class("graphene-settings-applet")

        # Init applet buttons
        self.box = Gtk.Box.new(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        self.box.set_homogeneous(True)
        self.box.set_orientation(Gtk.Orientation.HORIZONTAL)
        self.box.pack_end(GrapheneBatteryIcon(), False, False, 0)
        self.box.pack_end(GrapheneNetworkIcon(), False, False, 0)
        self.box.pack_end(GrapheneVolumeIcon(), False, False, 0)
        self.box.pack_end(Gtk.Image.new_from_icon_name("emblem-system-symbolic", Gtk.IconSize.MENU), False, False, 0)
        
        self.add(self.box)
        self.show_all()

        self.connect("button_press_event", self.on_clicked)
        self.popup.connect("hide", self.on_settings_menu_hide)
        
    def on_clicked(self, button, event):
        self.get_style_context().add_class("clicked")
        self.popup.show()
        return Gdk.EVENT_STOP # Required to keep the button from staying highlighted permanently 

    def on_settings_menu_hide(self, window):
        self.get_style_context().remove_class("clicked")


class GrapheneSettingsPopup(Gtk.Window):
    __gtype_name__ = 'GrapheneSettingsPopup'
    
    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        
        self.set_type_hint(Gdk.WindowTypeHint.POPUP_MENU) # Must be POPUP_MENU or else z-sorting conflicts with the dock
        self.connect("map", self.on_mapped)
        self.connect("button_press_event", self.on_mouse_event)
        self.get_screen().connect("monitors-changed", self.on_monitors_changed)
        self.set_role("GraphenePopup") # Tells graphene-wm this is a popup
        self.get_style_context().add_class("graphene-settings-popup")

        # Layout
        self.layout = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.layout.get_style_context().add_class("panel")
        self.layout.set_halign(Gtk.Align.FILL)
        self.layout.set_valign(Gtk.Align.FILL)
        
        # Current session info (profile name, profile icon)
        profileNameLabel = users.ProfileNameLabel()
        profileNameLabel.set_name("profile-name-label")
        profileNameLabel.set_valign(Gtk.Align.CENTER)
        profileNameLabel.set_halign(Gtk.Align.CENTER)

        # self.profileIcon = Gtk.Image.new_from_icon_name("", Gtk.IconSize.DIALOG)
        # self.profileIcon.set_name("profile-icon")
        
        # Create box for session control buttons (logout, shutdown, (temp)settings)
        sessionControlBox = Gtk.Box.new(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        sessionControlBox.set_halign(Gtk.Align.CENTER)
        sessionControlBox.set_name("session-control-box")
        
        logoutButton = Gtk.Button.new_from_icon_name("system-shutdown-symbolic", Gtk.IconSize.DND)
        logoutButton.connect("clicked", self.on_logout_button_clicked)
        sessionControlBox.pack_start(logoutButton, False, False, 0)
        
        # Create top box for session info and control
        global sessionbox
        self.sessionBox = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        self.sessionBox.pack_start(profileNameLabel, False, False, 0)
        self.sessionBox.pack_start(sessionControlBox, False, False, 0)
        self.sessionBox.set_name("session-box")
        self.layout.pack_start(self.sessionBox, False, False, 0)
        
        # Box for all the system settings (below the session info)
        self.settingsBox = GrapheneSettingsView(self, self.on_scrolled)
        self.layout.pack_start(self.settingsBox, True, True, 0)

        # Add layout to window
        self.sessionBox.show_all()
        self.layout.show()
        self.add(self.layout)
    
    def show(self):
        self.panel.capture_screen()
        self.settingsBox.show()
        super().show()
        self.grab_add() # Mouse events over the capture will be grabbed too
        
    def hide(self):
        self.grab_remove()
        super().hide()
        self.settingsBox.hide()
        self.panel.end_capture()

    def on_mapped(self, popup):
        self.get_window().focus(GdkX11.x11_get_server_time(self.get_window()))
        self.update_size()
    
    def on_monitors_changed(self, screen):
        self.update_size()

    def on_mouse_event(self, widget, event):
        if event.window.get_toplevel() != self.get_window():
            self.hide()
        return Gdk.EVENT_PROPAGATE

    def update_size(self):
        rect = self.panel.get_screen().get_monitor_geometry(self.panel.get_monitor())
        if self.get_window():
            self.get_window().move_resize(rect.x + rect.width - (rect.width/6), rect.y, rect.width/6, rect.height-self.panel.get_height())

    def on_logout_button_clicked(self, button):
        self.hide()
        self.panel.logout()
    
    def on_shutdown_button_clicked(self, button, event):
        self.hide()
        print("Shutdown/restart not implemented yet")

    def on_settings_button_clicked(self, button, event):
        self.hide()
        subprocess.Popen("gnome-control-center", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
    def on_scrolled(self, adj):
        if adj.get_value() > 5:
            self.sessionBox.get_style_context().add_class("shadow")
        else:
            self.sessionBox.get_style_context().remove_class("shadow")
            
class GrapheneSettingsView(Graphene.MaterialBox):
    __gtype_name__ = 'GrapheneSettingsView'

    def __init__(self, window, on_scrolled_cb):
        super().__init__()
        self.get_style_context().add_class("graphene-settings-view")

        self.centerLayout = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.window = window
        
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC) # Keeps the popup width from being to small
        scrolled.add(self.centerLayout)
        scrolled.get_vadjustment().connect("value-changed", on_scrolled_cb)
        
        self.add_sheet(scrolled, Graphene.MaterialBoxSheetLocation.CENTER)
        
        # self.centerLayout.pack_start(Gtk.Label.new(), False, False, 0)
        # self.centerLayout.pack_start(Gtk.Label.new("Eventually, there will be in-line"), False, False, 0)
        # self.centerLayout.pack_start(Gtk.Label.new("settings panels instead of linking "), False, False, 0)
        # self.centerLayout.pack_start(Gtk.Label.new("to GNOME Control Center."), False, False, 0)

        self.add_settings_category_label("Personal")
        self.add_setting_widget("Background",       "preferences-desktop-wallpaper",    True,  "background")
        self.add_setting_widget("Notifications",    "preferences-system-notifications", True,  "notifications")
        self.add_setting_widget("Privacy",          "preferences-system-privacy",       False, "privacy")
        self.add_setting_widget("Region & Language","preferences-desktop-locale",       False, "region")
        self.add_setting_widget("Search",           "preferences-system-search",        False, "search", bottomSeparator=True)
        self.add_settings_category_label("Hardware")
        self.add_setting_widget("Bluetooth",        "bluetooth",                        True,  "bluetooth")
        self.add_setting_widget("Color",            "preferences-color",                False, "color")
        self.add_setting_widget("Displays",         "preferences-desktop-display",      False, "display")
        self.add_setting_widget("Keyboard",         "input-keyboard",                   False, "keyboard")
        self.add_setting_widget("Mouse & Touchpad", "input-mouse",                      False, "mouse")
        self.add_setting_widget("Network",          "network-workgroup",                True,  "network")
        self.add_setting_widget("Power",            "gnome-power-manager",              False, "power")
        self.add_setting_widget("Printers",         "printer",                          False, "printers")
        self.add_setting_widget("Sound",            "sound",                            True,  "sound")
        self.add_setting_widget("Wacom Tablet",     "input-tablet",                     False, "wacom", bottomSeparator=True)
        self.add_settings_category_label("System")
        self.add_setting_widget("Date & Time",      "preferences-system-time",          False, "datetime")
        self.add_setting_widget("Details",          "applications-system",              False, "info")
        self.add_setting_widget("Sharing",          "preferences-system-sharing",       False, "sharing")
        self.add_setting_widget("Universal",        "preferences-desktop-accessibility",False, "universal-access")
        self.add_setting_widget("Users",            "system-users",                     False, "user-accounts", bottomSeparator=True)

        scrolled.show_all()

    def add_settings_category_label(self, title):
        label = Gtk.Label.new(title)
        label.set_halign(Gtk.Align.START)
        label.get_style_context().add_class("group-label")
        self.centerLayout.pack_start(label, False, False, 0)

    def add_setting_widget(self, title, iconName, toggleable, panel, bottomSeparator=False):
        box = Graphene.MaterialBox.new()
        button = Gtk.Button.new()
        button.materialbox = box
        
        button.get_style_context().add_class("settings-widget-button")
        button.panel = panel
        button.connect("clicked", self.on_settings_widget_clicked)

        buttonBox = Gtk.Box.new(Gtk.Orientation.HORIZONTAL, 7)
        buttonBox.pack_start(Gtk.Image.new_from_icon_name(iconName, Gtk.IconSize.DND), True, True, 7)
        label = Gtk.Label.new(title)
        label.set_yalign(0.5)
        buttonBox.pack_start(label, True, True, 0)
        buttonBox.set_halign(Gtk.Align.START)
        button.add(buttonBox)
        box.add_sheet(button, Graphene.MaterialBoxSheetLocation.CENTER)
        box.show_all() # Need to show all now before adding the toggle switch. I'll figure out why eventually, probably.

        if toggleable:
            toggle = Gtk.Switch.new()
            toggle.set_valign(Gtk.Align.CENTER)
            toggle.get_style_context().add_class("settings-widget-switch")
            toggle.props.margin = 5
            toggle.props.margin_right += 10
            box.add_sheet(toggle, Graphene.MaterialBoxSheetLocation.RIGHT)
            toggle.show_all()

        sep = Gtk.Separator.new(Gtk.Orientation.HORIZONTAL)
        sep.get_style_context().add_class("list-item-separator")
        self.centerLayout.pack_start(sep, False, False, 0)

        self.centerLayout.pack_start(box, False, False, 0)
        
        if bottomSeparator:
            sep = Gtk.Separator.new(Gtk.Orientation.HORIZONTAL)
            sep.get_style_context().add_class("list-item-separator")
            self.centerLayout.pack_start(sep, False, False, 0)
    
    def on_settings_widget_clicked(self, button):
        if isinstance(button.panel, str):
            subprocess.Popen(["gnome-control-center", button.panel])
            self.window.hide()
        else:
            panel = button.panel()
            self.add_sheet(panel, Graphene.MaterialBoxSheetLocation.RIGHT)
            self.show_sheet(panel)
        
    def hide(self):
        super().hide()
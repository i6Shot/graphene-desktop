# graphene-desktop
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
# Plugin loaded by VosPanel to add a system settings widget. This is a built-in plugin,
# and is automatically put on the far right side of the panel (but before the clock).

import gi
gi.require_version('AccountsService', '1.0')
from gi.repository import GLib, GObject, Gio, Gtk, Gdk, GdkPixbuf, Vos, AccountsService
import threading, time, subprocess
from battery import VosBatteryIcon, VosBatteryInfo
from volume import VosVolumeIcon, VosVolumeSlider
import users

class VosSettingsExtension(GObject.Object, Vos.AppletExtension):
    __gtype_name__ = 'VosSettingsExtension'

    def do_get_widget(self, panel):
        return VosSettingsApplet(panel)


class VosSettingsApplet(Gtk.Button):
    __gtype_name__ = 'VosSettingsApplet'

    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        self.popup = VosSettingsPopup(panel)
        
        # Init applet buttons
        self.box = Gtk.Box.new(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        self.box.set_homogeneous(True)
        self.box.set_orientation(Gtk.Orientation.HORIZONTAL)
        self.box.pack_end(VosBatteryIcon(), False, False, 0)
        # self.box.pack_end(Gtk.Image.new_from_icon_name("network-wireless-symbolic", Gtk.IconSize.MENU), False, False, 0)
        self.box.pack_end(VosVolumeIcon(), False, False, 0)
        self.box.pack_end(Gtk.Image.new_from_icon_name("avatar-default-symbolic", Gtk.IconSize.MENU), False, False, 0)
        
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


class VosSettingsPopup(Gtk.Window):
    __gtype_name__ = 'VosSettingsPopup'
    
    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        
        self.set_type_hint(Gdk.WindowTypeHint.POPUP_MENU) # Must be POPUP_MENU or else z-sorting conflicts with the dock
        self.connect("map", self.on_mapped)
        self.connect("button_press_event", self.on_mouse_event)
        self.get_screen().connect("monitors-changed", self.on_monitors_changed)

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
        
        logoutButton = Gtk.Button.new_from_icon_name("application-exit-symbolic", Gtk.IconSize.DND)
        logoutButton.connect("button_press_event", self.on_logout_button_clicked)
        sessionControlBox.pack_start(logoutButton, False, False, 0)
        
        shutdownButton = Gtk.Button.new_from_icon_name("system-shutdown-symbolic", Gtk.IconSize.DND)
        shutdownButton.connect("button_press_event", self.on_shutdown_button_clicked)
        sessionControlBox.pack_start(shutdownButton, False, False, 0)
        
        settingsButton = Gtk.Button.new_from_icon_name("emblem-system-symbolic", Gtk.IconSize.DND)
        settingsButton.connect("button_press_event", self.on_settings_button_clicked)
        sessionControlBox.pack_start(settingsButton, False, False, 0)
        
        # Create top box for session info and control
        sessionBox = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        sessionBox.pack_start(profileNameLabel, False, False, 0)
        sessionBox.pack_start(sessionControlBox, False, False, 0)
        sessionBox.set_name("session-box")
        self.layout.pack_start(sessionBox, False, False, 0)
        
        # Temp
        self.layout.pack_end(VosVolumeSlider(), False, False, 0)
        
        # Add layout to window
        self.layout.show_all()
        self.add(self.layout)
    
    def show(self):
        self.panel.capture_screen()
        super().show()
        self.grab_add() # Mouse events over the capture will be grabbed too

    def hide(self):
        self.grab_remove()
        super().hide()
        self.panel.end_capture()

    def on_mapped(self, popup):
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
            self.get_window().move_resize(rect.x + rect.width - (rect.width/5), rect.y, rect.width/5, rect.height-self.panel.get_height())

    def on_logout_button_clicked(self, button, event):
        self.hide()
        self.panel.logout()
    
    def on_shutdown_button_clicked(self, button, event):
        self.hide()
        print("Shutdown/restart not implemented yet")

    def on_settings_button_clicked(self, button, event):
        self.hide()
        subprocess.Popen("gnome-control-center", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
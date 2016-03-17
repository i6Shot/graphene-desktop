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

class VosSettingsExtension(GObject.Object, Vos.AppletExtension):
    __gtype_name__ = 'VosSettingsExtension'

    def do_get_widget(self, panel):
        return VosSettingsApplet(panel)
    
class VosSettingsPopup(Gtk.Window):
    __gtype_name__ = 'VosSettingsPopup'
    
    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        self.set_type_hint(Gdk.WindowTypeHint.POPUP_MENU) # Must be POPUP_MENU or else z-sorting conflicts with the dock
        self.connect("button_press_event", self.on_mouse_event)

        # Layout
        self.popupLayout = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.popupLayout.get_style_context().add_class("panel")
        self.popupLayout.set_halign(Gtk.Align.FILL)
        self.popupLayout.set_valign(Gtk.Align.FILL)
        self.create_layout()
        self.popupLayout.show_all()
        self.add(self.popupLayout)

        # Get accountsservice user manager
        manager = AccountsService.UserManager.get_default()
        if manager.no_service():
            # TODO: Try again?
            print("** settings.py: Critical: Cannot access AccountsSerivce. Make sure accounts-daemon is running.\n")
        elif manager.props.is_loaded:
            self.on_user_manager_notify_is_loaded(manager, None)
        else:
            manager.connect("notify::is-loaded", self.on_user_manager_notify_is_loaded)
    
    def create_layout(self):
        self.profileNameLabel = Gtk.Label.new("Unknown User")
        self.profileNameLabel.set_name("profile-name-label")
        self.profileNameLabel.set_valign(Gtk.Align.CENTER)
        self.profileNameLabel.set_halign(Gtk.Align.CENTER)

        self.profileIcon = Gtk.Image.new_from_icon_name("", Gtk.IconSize.DIALOG)
        self.profileIcon.set_name("profile-icon")
        
        # userNameBox = Gtk.Box.new(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        # userNameBox.pack_start(self.profileIcon, False, False, 0)
        # userNameBox.pack_start(self.profileNameLabel, True, True, 0)

        logoutBox = Gtk.Box.new(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        logoutButton = Gtk.Button.new_from_icon_name("application-exit-symbolic", Gtk.IconSize.DND)
        logoutButton.connect("button_press_event", self.on_logout_button_clicked)
        logoutBox.pack_start(logoutButton, False, False, 0)
        # lockButton = Gtk.Button.new_from_icon_name("system-lock-screen-symbolic", Gtk.IconSize.DND)
        # lockButton.connect("button_press_event", self.on_logout_button_clicked)
        # logoutBox.pack_start(lockButton, False, False, 0)
        shutdownButton = Gtk.Button.new_from_icon_name("system-shutdown-symbolic", Gtk.IconSize.DND)
        shutdownButton.connect("button_press_event", self.on_shutdown_button_clicked)
        logoutBox.pack_start(shutdownButton, False, False, 0)
        settingsButton = Gtk.Button.new_from_icon_name("emblem-system-symbolic", Gtk.IconSize.DND)
        settingsButton.connect("button_press_event", self.on_settings_button_clicked)
        logoutBox.pack_start(settingsButton, False, False, 0)
        logoutBox.set_halign(Gtk.Align.CENTER)
        logoutBox.set_name("username-box")

        userBox = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        userBox.pack_start(self.profileNameLabel, False, False, 0)
        userBox.pack_start(logoutBox, False, False, 0)
        userBox.set_name("profile-box")

        self.popupLayout.pack_start(userBox, False, False, 0)
        
        # Temp
        self.popupLayout.pack_end(VosVolumeSlider(), False, False, 0)
        
        tempInfoBox = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        tempInfoBox.pack_end(Gtk.Label.new(""), False, False, 0)
        tempInfoBox.pack_end(Gtk.Label.new("Programming is hard :("), False, False, 0)
        tempInfoBox.pack_end(Gtk.Label.new("But not yet."), False, False, 0)
        tempInfoBox.pack_end(Gtk.Label.new("Eventually it will replace GNOME Control Center."), False, False, 0)
        tempInfoBox.pack_end(Gtk.Label.new("You will be able to change settings here!"), False, False, 0)
        tempInfoBox.pack_end(Gtk.Label.new(""), False, False, 0)
        tempInfoBox.pack_end(Gtk.Label.new("You're running VeltOS (Technical Preview 2)!"), False, False, 0)
        self.popupLayout.set_center_widget(tempInfoBox)
    
    def on_user_manager_notify_is_loaded(self, manager, pspec):
        if not manager.props.is_loaded:
            return
        
        currentUsername = GLib.getenv("USER")
        if not currentUsername:
            print("** settings.py: Critical: Cannot determine current user (env variable $USER).\n")
            return
            
        currentUser = manager.get_user(currentUsername)
        
        iconFile = currentUser.get_icon_file()
        if iconFile:
            pixbuf = GdkPixbuf.Pixbuf.new_from_file(iconFile)
            pixbuf = pixbuf.scale_simple(60, 60, GdkPixbuf.InterpType.BILINEAR)
            self.profileIcon.set_from_pixbuf(pixbuf)
        
        self.profileNameLabel.set_text(currentUser.get_real_name())
    
    def on_logout_button_clicked(self, button, event):
        self.hide()
        self.panel.logout()
    
    def on_shutdown_button_clicked(self, button, event):
        self.hide()
        print("Shutdown/restart not implemented yet")

    def on_settings_button_clicked(self, button, event):
        self.hide()
        subprocess.Popen("gnome-control-center", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    def on_mouse_event(self, widget, event):
        if event.window.get_toplevel() != self.get_window():
            self.hide()
        return Gdk.EVENT_PROPAGATE
        
    def show(self):
        super().show()
        self.grab_add()

    def hide(self):
        self.grab_remove()
        super().hide()

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
        #print(GLib.get_system_config_dirs())
        
    def on_clicked(self, button, event):
        self.show_settings_menu()
        return Gdk.EVENT_STOP # Required to keep the button from staying highlighted permanently 

    def show_settings_menu(self):
        rect = self.panel.get_screen().get_monitor_geometry(self.panel.get_monitor())
        self.popup.move(rect.width-(rect.width/4),0)
        self.popup.set_size_request(rect.width/4, rect.height-self.panel.get_height())
        
        self.get_style_context().add_class("clicked")
        self.panel.capture_screen()
        self.popup.show()

    def on_settings_menu_hide(self, window):
        self.panel.end_capture()
        self.get_style_context().remove_class("clicked")
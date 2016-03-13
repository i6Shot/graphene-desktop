# vos-desktop
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
# launcher.py/.plugin
# Plugin loaded by VosPanel to add a application launcher widget. This is a built-in plugin, and is automatically put on the far
# left side of the panel.

import gi, os, subprocess, time
gi.require_version('GMenu', '3.0')
from gi.repository import GLib, GObject, Gtk, Gdk, GMenu, Vos

class VosLauncherExtension(GObject.Object, Vos.AppletExtension):
    __gtype_name__ = 'VosLauncherExtension'

    def do_get_widget(self, panel):
        return VosLauncherApplet(panel)

class VosLauncherApplet(Gtk.Button):
    __gtype_name__ = 'VosLauncherApplet'

    popup = Gtk.Window.new(Gtk.WindowType.TOPLEVEL)
    appTree = GMenu.Tree.new("gnome-applications.menu", GMenu.TreeFlags.SORT_DISPLAY_NAME)
    popupLayout = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=0)
    appListBox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
    searchBar = Gtk.SearchEntry.new()
    filter = ""

    def __init__(self, panel):
        super().__init__()
        self.panel = panel

        # Init button
        self.set_label("")
        self.set_name("launcher-applet-button")
        self.connect("button_press_event", self.on_applet_button_click)

        img = Gtk.Image.new_from_icon_name("open-menu-symbolic", Gtk.IconSize.INVALID);
        img.set_pixel_size(32)
        self.set_image(img)
        self.set_always_show_image(True)
        self.show_all()

        # Layout
        self.popupLayout.set_halign(Gtk.Align.FILL)
        self.popupLayout.set_valign(Gtk.Align.FILL)

        # Create popup
        self.popup.set_type_hint(Gdk.WindowTypeHint.POPUP_MENU) # Must be POPUP_MENU or else z-sorting conflicts with the dock
        self.popup.add(self.popupLayout)
        self.popup.connect("map", self.on_popup_mapped)
        self.popup.connect("button_press_event", self.on_mouse_event)
        self.popup.connect("key_press_event", self.on_key_event)
        self.popup.connect("key_release_event", self.on_key_event)
        
        # Search bar
        self.searchBar.connect("changed", self.on_search_changed)
        self.searchBar.connect("activate", self.on_search_enter)
        self.popupLayout.pack_start(self.searchBar, False, False, 0)

        # General applet box
        self.appListBox.set_name("launcher-app-list-box")
        self.appListBox.set_margin_top(5)
        self.appListBox.set_margin_start(5)
        self.appListBox.set_margin_end(5)
        scrolled = Gtk.ScrolledWindow.new(None, None)
        scrolled.add(self.appListBox)
        self.popupLayout.pack_start(scrolled, True, True, 0)
        self.popupLayout.show_all()

        # Load applications
        self.appTree.load_sync()

    def on_applet_button_click(self, button, event):
        self.show_applist()
        
    def show_applist(self):
        self.populate_applist()

        rect = self.panel.get_screen().get_monitor_geometry(self.panel.get_monitor())
        self.popup.set_size_request(rect.width/4, rect.height-self.panel.get_height())
        
        # self.get_style_context().add_class("clicked-button")
        self.panel.capture_screen()
        self.popup.show()
        self.popup.grab_add()

        GLib.idle_add(self.reload_apptree) # Reload widgets when nothing else is going on
        
    def hide_applist(self):
        if not self.popup.get_mapped(): # Don't do anything if the popup isn't open
            return
        
        self.popup.grab_remove()
        self.popup.hide()
        self.panel.end_capture()
        # self.get_style_context().remove_class("clicked-button")
        
        # Because the capture starts while the mouse is over the button, it never receives
        # leave-notify-event, and it stays highlighted. This stops the highlight. It's a bit
        # weird, but nothing else works. This doesn't seem to transition the dehighlight either. (TODO?)
        # state = self.get_state_flags() & ~(Gtk.StateFlags.PRELIGHT | Gtk.StateFlags.ACTIVE)
        # self.set_state_flags(Gtk.StateFlags(state), True)

    def on_popup_mapped(self, popup):
        # Force the WM to give the popup keyboard focus even though it's a DOCK
        # TODO: Figure out how to not remove focus from other toplevel windows
        self.popup.get_window().focus(Gdk.CURRENT_TIME)
    
    def reload_apptree(self):
        self.appTree.load_sync()
        return False # Return False so that idle_add doesn't loop this continuously
    
    def populate_applist(self):
        widgets = self.appListBox.get_children()
        for widget in widgets:
            widget.destroy()

        self.populate_applist_directory(self.appTree.get_root_directory())

    def populate_applist_directory(self, directory):
        count = 0
        it = directory.iter();
        while True:
            type = it.next()
            if type == GMenu.TreeItemType.INVALID:
                break

            if type == GMenu.TreeItemType.ENTRY:
                appInfo = it.get_entry().get_app_info()

                if not self.filter in appInfo.get_display_name().lower():
                    continue

                button = Gtk.Button.new_with_label(appInfo.get_display_name())

                img = Gtk.Image.new_from_gicon(appInfo.get_icon(), Gtk.IconSize.LARGE_TOOLBAR);
                button.set_image(img)
                button.set_always_show_image(True)

                button.connect("clicked", self.on_applist_item_clicked, appInfo)
                button.show()
                button.get_style_context().add_class("launcherlist-button")
                self.appListBox.pack_start(button, False, False, 0)
                count += 1

            elif type == GMenu.TreeItemType.DIRECTORY:
                directory = it.get_directory()

                label = Gtk.Label.new(directory.get_name())
                label.set_halign(Gtk.Align.START)
                label.get_style_context().add_class("launcherlist-label")
                self.appListBox.pack_start(label, False, False, 0)

                subcount = self.populate_applist_directory(directory)
                if subcount > 0:
                    label.show()
                    count += subcount
                else:
                    label.destroy()

        return count

    def on_search_changed(self, s):
        self.filter = self.searchBar.get_text().lower()
        self.populate_applist()

    def on_search_enter(self, s):
        if self.searchBar.get_text() and self.searchBar.is_focus(): # Enter opens first thing only if there is a filter
            widgets = self.appListBox.get_children()
            for widget in widgets:
                if isinstance(widget, Gtk.Button):
                    widget.clicked()
                    break

    def on_key_event(self, widget, event):
        if not self.searchBar.is_focus():
            return self.searchBar.handle_event(event)
        return Gdk.EVENT_PROPAGATE

    def on_mouse_event(self, widget, event):
        if event.window.get_toplevel() != self.popup.get_window():
            self.hide_applist()
        return Gdk.EVENT_PROPAGATE

    def on_applist_item_clicked(self, button, appInfo):
        self.searchBar.set_text("")
        self.hide_applist()
        subprocess.Popen(appInfo.get_executable(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
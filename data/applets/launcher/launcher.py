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
# launcher.py/.plugin
# Plugin loaded by VosPanel to add a application launcher widget. This is a built-in plugin, and is automatically put on the far
# left side of the panel.

import gi, os, subprocess, time
gi.require_version('GMenu', '3.0')
gi.require_version('GdkX11', '3.0')
from gi.repository import GLib, GObject, Gtk, Gdk, GdkX11, GMenu, Vos

class VosLauncherExtension(GObject.Object, Vos.AppletExtension):
    __gtype_name__ = 'VosLauncherExtension'

    def do_get_widget(self, panel):
        return VosLauncherApplet(panel)


class VosLauncherApplet(Gtk.Button):
    __gtype_name__ = 'VosLauncherApplet'

    def __init__(self, panel):
        super().__init__()
        self.panel = panel

        # Init button
        self.set_label("")
        self.connect("button_press_event", self.on_applet_button_click)

        img = Gtk.Image.new_from_icon_name("open-menu-symbolic", Gtk.IconSize.INVALID);
        img.set_pixel_size(32)
        self.set_image(img)
        self.set_always_show_image(True)
        self.show_all()

        # Create popup
        self.popup = VosLauncherPopup(panel)
        self.popup.connect("hide", self.on_popup_hide)

    def on_applet_button_click(self, button, event):
        self.get_style_context().add_class("clicked")
        self.popup.show()
        return Gdk.EVENT_STOP # Required to keep the button from staying highlighted permanently 
    
    def on_popup_hide(self, popup):
        self.panel.end_capture()
        self.get_style_context().remove_class("clicked")


class VosLauncherPopup(Gtk.Window):
    __gtype_name__ = 'VosLauncherPopup'

    def __init__(self, panel):
        super().__init__()
        self.panel = panel

        self.set_type_hint(Gdk.WindowTypeHint.POPUP_MENU) # Must be POPUP_MENU or else z-sorting conflicts with the dock
        self.connect("map", self.on_mapped)
        self.connect("button_press_event", self.on_mouse_event)
        self.connect("key_press_event", self.on_key_event)
        self.connect("key_release_event", self.on_key_event)
        self.get_screen().connect("monitors-changed", self.on_monitors_changed)
        self.set_role("GraphenePopup") # Tells graphene-wm this is a popup

        # Layout
        self.popupLayout = Gtk.Box.new(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.popupLayout.get_style_context().add_class("panel")
        self.popupLayout.set_halign(Gtk.Align.FILL)
        self.popupLayout.set_valign(Gtk.Align.FILL)
        self.add(self.popupLayout)
        
        # Search bar
        self.searchBar = Gtk.SearchEntry.new()
        self.filter = ""
        self.searchBar.connect("changed", self.on_search_changed)
        self.searchBar.connect("activate", self.on_search_enter)
        self.popupLayout.pack_start(self.searchBar, False, False, 0)

        # General applet box
        self.appListView = VosAppListView()
        self.appListView.connect("launch_app", self.on_applist_item_clicked)
        self.popupLayout.pack_start(self.appListView, True, True, 0)
        self.popupLayout.show_all()
    
    def show(self):
        self.appListView.refresh_applist(self.filter)
        self.panel.capture_screen()
        super().show()
        self.grab_add()
    
    def hide(self):
        self.grab_remove()
        super().hide()
        self.panel.end_capture()
    
    def on_mapped(self, popup):
        # Force the WM to give the popup keyboard focus even though it's a DOCK
        # TODO: Figure out how to not remove focus from other toplevel windows
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
            self.get_window().move_resize(rect.x, rect.y, rect.width/5, rect.height-self.panel.get_height())

    def on_search_changed(self, s):
        self.filter = self.searchBar.get_text().lower()
        self.appListView.populate_applist(self.filter)

    def on_search_enter(self, s):
        if self.searchBar.get_text() and self.searchBar.is_focus(): # Enter opens first thing only if there is a filter
            self.appListView.launch_first()
    
    def on_applist_item_clicked(self, view, button, appInfo):
        self.searchBar.set_text("")
        self.hide()
        subprocess.Popen(appInfo.get_executable(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def on_key_event(self, widget, event):
        if not self.searchBar.is_focus():
            return self.searchBar.handle_event(event)
        return Gdk.EVENT_PROPAGATE
        

class VosAppListView(Gtk.ScrolledWindow):
    __gtype_name__ = 'VosAppListView'

    __gsignals__ = { 'launch_app': (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, (GObject.TYPE_OBJECT, GObject.TYPE_OBJECT)) }

    def __init__(self):
        super().__init__()
        
        self.appListBox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        self.appListBox.props.margin = 5
        
        self.add(self.appListBox)
        self.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC) # Keeps the popup width from being to small
        
        # Load applications
        self.appTree = GMenu.Tree.new("gnome-applications.menu", GMenu.TreeFlags.SORT_DISPLAY_NAME)
        self.refresh_applist()
        
    def refresh_applist(self, filter=""):
        self.appTree.load_sync()
        self.populate_applist(filter)
        
    def populate_applist(self, filter=""):
        widgets = self.appListBox.get_children()
        for widget in widgets:
            widget.destroy()

        self.populate_applist_directory(self.appTree.get_root_directory(), filter)
        
    def populate_applist_directory(self, directory, filter=""):
        count = 0
        it = directory.iter();
        while True:
            type = it.next()
            if type == GMenu.TreeItemType.INVALID:
                break

            if type == GMenu.TreeItemType.ENTRY:
                appInfo = it.get_entry().get_app_info()

                if not filter in appInfo.get_display_name().lower():
                    continue

                button = Gtk.Button.new_with_label(appInfo.get_display_name())
                button.set_image(Gtk.Image.new_from_gicon(appInfo.get_icon(), Gtk.IconSize.LARGE_TOOLBAR))
                button.set_always_show_image(True)
                button.get_child().set_halign(Gtk.Align.START)
                button.connect("clicked", self.on_applist_item_clicked, appInfo)
                button.show()
                self.appListBox.pack_start(button, False, False, 0)

                count += 1

            elif type == GMenu.TreeItemType.DIRECTORY:
                directory = it.get_directory()

                label = Gtk.Label.new(directory.get_name())
                label.set_halign(Gtk.Align.START)
                label.get_style_context().add_class("directory-label")
                self.appListBox.pack_start(label, False, False, 0)

                subcount = self.populate_applist_directory(directory, filter)
                if subcount > 0:
                    label.show()
                    count += subcount
                else:
                    label.destroy()

        return count
        
    def on_applist_item_clicked(self, button, appInfo):
        self.emit("launch_app", button, appInfo)
        
    def launch_first(self):
        widgets = self.appListBox.get_children()
        for widget in widgets:
            if isinstance(widget, Gtk.Button):
                widget.clicked()
                break
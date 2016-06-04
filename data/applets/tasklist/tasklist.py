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
# tasklist.py/.plugin
# Plugin loaded by GraphenePanel to add a application launcher widget. This is a built-in plugin, and is automatically put on the far
# left side of the panel.

import gi, subprocess, bisect
gi.require_version('Wnck', '3.0')
from gi.repository import GObject, Gtk, Wnck, Graphene, Gio
from gi.repository.GdkPixbuf import InterpType

class GrapheneTaskListExtension(GObject.Object, Graphene.AppletExtension):
    __gtype_name__ = 'GrapheneTaskListExtension'
    
    def do_get_widget(self, panel):
        return GrapheneTaskListApplet(panel)
        
class GrapheneTaskListApplet(Gtk.Box):
    __gtype_name__ = 'GrapheneTaskListApplet'

    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        self.get_style_context().add_class("graphene-tasklist-applet")

        self.buttons = {}
            
        self.set_homogeneous(False)
        self.set_orientation(Gtk.Orientation.HORIZONTAL)
        
        screen = Wnck.Screen.get_default()
        screen.connect("window_opened", self.on_window_opened)
        screen.connect("window_closed", self.on_window_closed)
        screen.connect("active_window_changed", self.on_active_window_changed)
        
        # On the first launch, this does nothing because Wnck hasn't loaded yet and the window_opened event
        # will take care of calling on_window_opened once it has loaded. (get_windows() returns empty)
        # On subsequent launches, window_opened won't be emitted at the start, so the windows have to be
        # loaded now (get_windows() does not return empty)
        windows = screen.get_windows()
        for window in windows:
            self.on_window_opened(screen, window)

        self.show_all()
        
    def on_window_opened(self, screen, window):
        if not window or window.is_skip_tasklist() or window in self.buttons:
            return
        
        # Create a new task list item for the window
       	button = Gtk.Button.new()
       	button.get_style_context().add_class("tasklist-button")
        button.connect("clicked", self.on_button_clicked, window)
        button.connect("size_allocate", self.on_button_size_allocate, window)
        
        
        # cname = .lower()
        # cname = cname[0].lower() + cname[1:]
        
        # print(cname)
        # info = Gio.DesktopAppInfo.new(cname + ".desktop")
        
        # TODO: Automatic scaling with Gtk?
        iconSize = max(min(self.panel.get_height() - 8, 32), 12)
        # scaled = window.get_icon().scale_simple(iconSize, iconSize, InterpType.BILINEAR)
        # icon = Gtk.Image.new_from_pixbuf(scaled)
        className = window.get_class_group_name()
        if className:
            className = className.lower()
        else:
            className = ""
        icon = Gtk.Image.new_from_icon_name(className, Gtk.IconSize.LARGE_TOOLBAR)
        # button.set_image(Gtk.Image.new_from_gicon(info.get_icon(), Gtk.IconSize.LARGE_TOOLBAR))
        button.set_image(icon)
        button.set_always_show_image(True)
        
        button.show()
        self.pack_start(button, False, False, 0)
        self.buttons[window] = button
        
        # Watch for the window needing attention
        window.connect("state_changed", self.on_window_state_changed)
    
    def on_window_closed(self, screen, window):
        if window and not window.is_skip_tasklist() and window in self.buttons:
            self.remove(self.buttons[window])
            del self.buttons[window]

    def on_window_state_changed(self, window, changed_mask, new_state):
        # TODO: Does this work correctly? Should highlight the window orange when it needs attention. Overriden by tasklist-active-window?
        if window.needs_attention():
            self.buttons[window].get_style_context().add_class("task-attention")
        else:
            self.buttons[window].get_style_context().remove_class("task-attention")

    def on_active_window_changed(self, screen, prevWindow):
        if prevWindow and prevWindow in self.buttons:
            self.buttons[prevWindow].get_style_context().remove_class("task-active")
        
        window = screen.get_active_window()
        if not window or window.is_skip_tasklist():
            return
        
        if window in self.buttons:
            self.buttons[window].get_style_context().add_class("task-active")
    
    def on_button_size_allocate(self, button, allocation, window):        
        # Set the window's icon geometry to the screen coordinates of the button
        toplevel = button.get_toplevel()
        a = button.translate_coordinates(toplevel, 0, 0)
        b = button.get_window()
        if b:
            b = b.get_root_coords(a[0], a[1])
        else:
            return
        window.set_icon_geometry(b[0], b[1], allocation.width, allocation.height)
        
    def on_button_clicked(self, button, window):
        time = Gtk.get_current_event_time()
        
        if window.is_minimized():
            window.unminimize(time)
            window.activate(time)
        elif window.is_active():
            window.minimize()
        else:
            window.activate(time)
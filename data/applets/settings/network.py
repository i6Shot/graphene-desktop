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
# wifi.py
# Wifi settings for the settings panel and system tray icon. Requires wicd.

import dbus
from gi.repository import GLib, GObject, Gtk, Gdk
from dbus.mainloop.glib import DBusGMainLoop

# TODO: Could this conflict with other dbus things in the panel?
DBusGMainLoop(set_as_default=True)

wicdStatuses = {"not_connected": 0, "connecting": 1, "wireless": 2, "wired": 3, "suspended": 4}

class GrapheneNetworkIcon(Gtk.Image):
    __gtype_name__ = 'GrapheneNetworkIcon'

    def __init__(self):
        super().__init__()
        
        self.set_from_icon_name("network-no-route-symbolic", Gtk.IconSize.MENU)
        
        try:
            systemBus = dbus.SystemBus()
            systemBus.add_signal_receiver(self.update, "StatusChanged", "org.wicd.daemon")

            daemon = dbus.Interface(systemBus.get_object("org.wicd.daemon", "/org/wicd/daemon"), "org.wicd.daemon")
            status, info = daemon.GetConnectionStatus()
            self.update(status, info)
        
        except dbus.DBusException:
            pass
            
    def update(self, status, info):
        iconName = "network"
        
        if status == wicdStatuses["not_connected"]:
            iconName += "-offline"
        
        elif status == wicdStatuses["connecting"]:
            iconName += "-" + info[0] + "-acquiring" # info[1] is 'wireless' or 'wired'
        
        elif status == wicdStatuses["wireless"]:
            strength = info[2]
            iconName += "-wireless-"
            
            if strength > 75:
                iconName += "signal-excellent"
            elif wireless_signal > 50:
                iconName += "signal-good"
            elif wireless_signal > 25:
                iconName += "signal-ok"
            elif wireless_signal > 0:
                iconName += "signal-weak"
            else:
                iconName += "signal-none"
                
        elif status == wicdStatuses["wired"]:
            iconName += "-wired"
        else:
            iconName += "-no-route"
            
        GLib.idle_add(self.set_from_icon_name, iconName + "-symbolic", Gtk.IconSize.MENU)
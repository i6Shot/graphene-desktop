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
# users.py
# User settings control/info for the settings panel

import gi
gi.require_version('AccountsService', '1.0')
from gi.repository import GLib, GObject, Gtk, Gio, AccountsService

def load_user_accounts_manager(on_notify_loaded_cb):
    if not callable(on_notify_loaded_cb):
        return
    
    manager = AccountsService.UserManager.get_default()
    if manager.no_service():
        # TODO: Try again?
        print("** settings.py: Critical: Cannot access AccountsSerivce. Make sure accounts-daemon is running.\n")
    elif manager.props.is_loaded:
        on_notify_loaded_cb(manager, None)
    else:
        manager.connect("notify::is-loaded", on_notify_loaded_cb)


class ProfileNameLabel(Gtk.Label):
    __gtype_name__ = 'CurrentUserNameLabel'

    def __init__(self, username=None):
        super().__init__()
        self.username = username
        self.set_text("Unknown User")
        
        if not self.username:
            self.username = GLib.getenv("USER")
            if not self.username:
                print("** users.py: Critical: Cannot determine current user (env variable $USER).\n")
                return
        
        self.user = None
        self.userChangedHandlerID = 0
        
        load_user_accounts_manager(self.on_user_manager_notify_loaded)
        
    def on_user_manager_notify_loaded(self, manager, x):
        if self.user and self.userChangedHandlerID > 0:
            self.user.disconnect(self.userChangedHandlerID)
            self.user = None
            self.userChangedHandlerID = 0
            
        if not manager.props.is_loaded:
            self.set_text("Unknown User")
            return
        
        self.user = manager.get_user(self.username)
        self.userChangedHandlerID = self.user.connect("changed", self.on_user_updated)
        self.on_user_updated(self.user)

    def on_user_updated(self, user):
        self.set_text(user.get_real_name())


# iconFile = currentUser.get_icon_file()
# if iconFile:
#     pixbuf = GdkPixbuf.Pixbuf.new_from_file(iconFile)
#     pixbuf = pixbuf.scale_simple(60, 60, GdkPixbuf.InterpType.BILINEAR)
#     self.profileIcon.set_from_pixbuf(pixbuf)
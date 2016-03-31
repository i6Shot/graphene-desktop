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

import gi, math, cairo, os.path
gi.require_version('AccountsService', '1.0')
from gi.repository import GLib, GObject, Gtk, Gdk, GdkPixbuf, Gio, AccountsService

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
    __gtype_name__ = 'ProfileNameLabel'

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


class ProfilePicture(Gtk.DrawingArea):
    __gtype_name__ = 'ProfilePicture'

    def __init__(self, username=None):
        super().__init__()
        self.username = username
        
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
            self.on_user_updated(None)
            return
        
        self.user = manager.get_user(self.username)
        self.userChangedHandlerID = self.user.connect("changed", self.on_user_updated)
        self.on_user_updated(self.user)

    def on_user_updated(self, user):
        self.queue_draw()
        
    def get_picture_pixmap(self):
        # First try to get picture from AccountsService
        if self.user:
            path = self.user.get_icon_file()
            try:
                return GdkPixbuf.Pixbuf.new_from_file(path), True
            except (GLib.Error, TypeError): 
                pass
                
        try:
            return GdkPixbuf.Pixbuf.new_from_file(os.path.expanduser("~%s/.face" % self.username)), True
        except GLib.Error: 
            pass
        
        try:
            theme = Gtk.IconTheme.get_default()
            size = min(self.get_allocated_width(), self.get_allocated_height())
            return theme.load_icon("system-users", size, 0), False
        except GLib.Error: 
            return None, True

    def do_draw(self, cr):        
        # Render background
        styleContext = self.get_style_context()
        width = self.get_allocated_width()
        height = self.get_allocated_height()
        Gtk.render_background(styleContext, cr, 0, 0, width, height)

        # The size of the final image
        size = min(width, height)

        # Create a image surface with the profile image, applying a white background
        # Scale it up a bit for supersampling
        db = cairo.ImageSurface(cairo.FORMAT_ARGB32, size*2, size*2)
        dbcr = cairo.Context(db)

        dbcr.arc(db.get_width()/2, db.get_height()/2, min(db.get_width(), db.get_height())/2, 0, 2*math.pi)
        dbcr.clip()
        dbcr.new_path()

        img, bg = self.get_picture_pixmap()

        if bg:
            dbcr.set_source_rgb(0.827,0.827,0.827)
            dbcr.paint()

        if img:
            dbcr.scale(db.get_width()/img.get_width(), db.get_height()/img.get_height())
            Gdk.cairo_set_source_pixbuf(dbcr, img, 0, 0)
            dbcr.paint()

        db.flush()

        # TODO: Maybe cache db so it doesn't have to recreate it every time it draws? Not really necessary right now

        # Render the image to the widget
        cr.scale(size/db.get_width(), size/db.get_height())
        cr.set_source_surface(db,
          (width/2)/(size/db.get_width())-db.get_width()/2,
          (height/2)/(size/db.get_height())-db.get_height()/2)
        cr.paint()
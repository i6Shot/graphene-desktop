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

import gi, math
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


class ProfilePicture(Gtk.Image):
    __gtype_name__ = 'ProfilePicture'

    def __init__(self, username=None):
        super().__init__()
        self.username = username
        self.on_user_updated(None)
        self.pictureSize = 0
        
        if not self.username:
            self.username = GLib.getenv("USER")
            if not self.username:
                print("** users.py: Critical: Cannot determine current user (env variable $USER).\n")
                return
        
        self.user = None
        self.userChangedHandlerID = 0
        
        load_user_accounts_manager(self.on_user_manager_notify_loaded)
    
    def do_size_allocate(self, allocation):
        self.set_allocation(allocation)
        self.pictureSize = min(allocation.width, allocation.height)
        self.on_user_updated(self.user)

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
        iconFile = None
        if user and self.pictureSize > 0:
            iconFile = user.get_icon_file()
            
        if iconFile:
            pixbuf = GdkPixbuf.Pixbuf.new_from_file(iconFile)
            pixbuf = self.pixbuf_circle_crop(pixbuf, self.pictureSize)
            self.set_from_pixbuf(pixbuf)
        else:
            self.set_from_icon_name("avatar-default-symbolic", Gtk.IconSize.DIALOG)
    
    # Takes the given pixbuf, scales it to a square and crops out the corners to make it a circle
    # This method is very inefficent and can't be used for any large images.
    def pixbuf_circle_crop(self, pixbuf, size):
        # Create a white background and composite the pixbuf onto that
        scaledSize = size*4 # Scale it up a bit for supersampling
        bgbuf = GdkPixbuf.Pixbuf.new(pixbuf.get_colorspace(), True, pixbuf.get_bits_per_sample(), scaledSize, scaledSize)
        bgbuf.fill(0xFFFFFFFF)
        pixbuf.composite(bgbuf, 0, 0, scaledSize, scaledSize, 0, 0, scaledSize/pixbuf.get_width(), scaledSize/pixbuf.get_height(), GdkPixbuf.InterpType.BILINEAR, 255)
        pixbuf = bgbuf
        
        # Crop the image to a circle
        pixels = pixbuf.get_pixels()
        height = pixbuf.get_height()
        width = pixbuf.get_width()
        numChannels = pixbuf.get_n_channels()
        
        centerRow = width/2
        centerCol = height/2
        radius = width/2
        
        newpixels = bytearray()
        i = 0
        for pixel in pixels:
            pixelIndex = int(i/numChannels)
            row = int(pixelIndex/width)
            col = pixelIndex % width
            
            dist = math.sqrt(math.pow(row-centerRow, 2) + math.pow(col-centerCol, 2))
            if dist < radius:
                newpixels.append(pixel)
            else:
                newpixels.append(0)
            i+=1
        
        newPixbuf = GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(newpixels), pixbuf.get_colorspace(),
            pixbuf.get_has_alpha(), pixbuf.get_bits_per_sample(), pixbuf.get_width(), pixbuf.get_height(),
            pixbuf.get_rowstride())
        
        newPixbuf = newPixbuf.scale_simple(size, size, GdkPixbuf.InterpType.BILINEAR) # Scale it back to requested size
        return newPixbuf
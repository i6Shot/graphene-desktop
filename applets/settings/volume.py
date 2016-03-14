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
# volume.py
# Creates a volume icon using SystemVolumeControl from libvos. Used in the settings panel.

from gi.repository import GLib, GObject, Gio, Gtk, Vos
import threading, time

class VosVolumeIcon(Gtk.Image):
	__gtype_name__ = 'VosVolumeIcon'
	
	def __init__(self):
		super().__init__()

		self.prevIconName = None

		self.get_style_context().add_class("battery-icon")
		self.set_from_icon_name("battery-full-charged-symbolic", Gtk.IconSize.MENU)
		
		self.volumeControl = Vos.SystemVolumeControl.new()
		self.volumeControl.connect("notify::volume", self.update)
		self.volumeControl.connect("notify::muted", self.update)

	def update(self, vc, x):
		iconName = "audio-volume-"
		volPercent = vc.get_volume()
		
		if vc.get_is_muted():
			iconName += "muted"
		elif volPercent >= 2/3:
			iconName += "high"
		elif volPercent >= 1/3:
			iconName += "medium"
		else:
			iconName += "low"

		iconName += "-symbolic";			

		if self.prevIconName != iconName:
			self.prevIconName = iconName
			GLib.idle_add(self.set_from_icon_name, iconName, Gtk.IconSize.MENU)
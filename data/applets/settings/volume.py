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
# volume.py
# Creates a volume icon using SystemVolumeControl from libgraphene. Used in the settings panel.

from gi.repository import GLib, GObject, Gio, Gtk, Graphene
import threading, time

class GrapheneVolumeIcon(Gtk.Image):
	__gtype_name__ = 'GrapheneVolumeIcon'
	
	def __init__(self):
		super().__init__()

		self.prevIconName = None

		self.set_from_icon_name("audio-volume-high-symbolic", Gtk.IconSize.MENU)
		
		self.volumeControl = Graphene.SystemVolumeControl.new()
		self.volumeControl.connect("notify::volume", self.update)
		self.volumeControl.connect("notify::muted", self.update)

	def update(self, vc, x):
		iconName = "audio-volume-"
		volPercent = vc.get_volume()
		
		if volPercent == 0 or vc.get_is_muted():
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
			
class GrapheneVolumeSlider(Gtk.Box):
	__gtype_name__ = 'GrapheneVolumeSlider'

	def __init__(self):
		super().__init__(orientation=Gtk.Orientation.HORIZONTAL)
		
		self.props.margin = 20
		
		self.volumeControl = Graphene.SystemVolumeControl.new()
		self.volumeControl.connect("notify::volume", self.update)
		
		self.volumeIcon = GrapheneVolumeIcon()
		self.volumeIcon.set_valign(Gtk.Align.START)

		self.slider = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0.0, 1.5, 0.1)
		self.slider.set_draw_value(False)
		self.slider.set_digits(1)
		self.slider.add_mark(1.0, Gtk.PositionType.BOTTOM, "100%")
		self.slider.connect("value-changed", self.on_value_changed)

		self.pack_start(self.volumeIcon, False, False, 0)
		self.pack_start(self.slider, True, True, 0)
		
	def on_value_changed(self, scale):
		value = self.slider.get_value()
		self.volumeControl.set_volume(value)
		if value == 0:
			self.volumeControl.set_is_muted(True)
		elif self.volumeControl.get_is_muted():
			self.volumeControl.set_is_muted(False)
		
	def update(self, vc, x):
		self.slider.set_value(vc.get_volume())
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
# clock.py/.plugin
# Plugin loaded by VosPanel to add a clock widget. This is a built-in plugin, and is automatically put on the far right side of
# the panel.

from gi.repository import GLib, GObject, Gtk, Vos
import datetime, threading, time

class VosClockExtension(GObject.Object, Vos.AppletExtension):
	__gtype_name__ = 'VosClockExtension'
	
	def do_get_widget(self, panel):
		return VosClockApplet()
		
class VosClockApplet(Gtk.Label):
	__gtype_name__ = 'VosClockApplet'
	
	def __init__(self):
		super().__init__()
		
		self.stopUpdates = False
		self.prevTimeString = None
		
		self.connect("destroy", self.on_destroy)
		self.updateThread = threading.Thread(target=self.update_thread)
		self.updateThread.start()
		self.show_all()

	def on_destroy(self, widget):
		self.stopUpdates = True
	
	def update_thread(self):
		while not self.stopUpdates:
			# Update the GUI to the current time
			dt = datetime.datetime.now()
			timeString = dt.strftime("%a %H:%M") # TODO: Locale formatting. %X?
			
			if timeString != self.prevTimeString:
				self.prevTimeString = timeString
				GLib.idle_add(self.set_label, timeString) # Idle add calls the function on repeat, but set_label returns None (False) so it stops
		 
			# Wait until the next second
			now = time.time()
			nextSecond = int(now) + 1
			time.sleep(nextSecond - now)
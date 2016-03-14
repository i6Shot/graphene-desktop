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
# battery.py
# Creates a battery icon and battery status monitoring class for the setting panel.

from gi.repository import GLib, GObject, Gio, Gtk, Vos
import threading, time

class VosBatteryInfo(GObject.Object):
	__gtype_name__ = 'VosBatteryInfo'

	__gsignals__ = { 'update': (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, (GObject.TYPE_INT, GObject.TYPE_STRING)) }

	def __init__(self):
		super().__init__()

		# TODO: Use a file monitoring method instead of polling
		# Attempted to use Gio.FileMonitor, which works for regualar files but doesn't seem to work to read battery info files
		# Maybe because they're hardlinked? However, G_FILE_MONITOR_WATCH_HARD_LINKS causes the monitor to segfault. No idea why.
		# 
		# GFile *file = g_file_new_for_path("/sys/class/power_supply/BAT0/status");
		# g_message("exists: %i", g_file_query_exists(file, NULL));
		# GError *error = NULL;
		# g_file_monitor(file, G_FILE_MONITOR_WATCH_HARD_LINKS, NULL, &error);
		# if(error) g_message("error: %s", error->message);
		# g_signal_connect(monitor, "changed", dir_changed, NULL);
		#
		# capacityFile = Gio.File.new_for_path("/sys/class/power_supply/BAT0/capacity")
		# print("exists %i" % (capacityFile.query_exists(None)))
		# monitor = capacityFile.monitor_file(Gio.FileMonitorFlags.NONE, None)
		# monitor.connect("changed", self.change)
		# print("cancelled: %i" % monitor.is_cancelled())

		self.stopUpdates = False
		self.prevStatus = None
		self.prevPercentage = None
		
		self.updateThread = threading.Thread(target=self.update_thread)
		self.updateThread.start()

	def __del__(self):
		self.stopUpdates = True
		super().__del__()

	# @GObject.Signal(arg_types=(int,str))
	# def my_signal(self, arg1, arg2):
	# 	print("my_signal", arg1, arg2)
	# 
	# def do_my_signal(self, arg1, arg2):
	# 	print("do_my_signal", arg1, arg2)
	# 
	# @GObject.Signal
	# def noarg_signal(self):
	# 	print("noarg_signal")

	def update_thread(self):
		while not self.stopUpdates:
			info = self.get_battery_info()
			if info['percentage'] != self.prevPercentage or info['status'] != self.prevStatus:
				self.prevPercentage = info['percentage']
				self.prevStatus = info['status']
				self.emit("update", info['percentage'], info['status'])
			time.sleep(3) # seconds

	def get_battery_info(self):
		status = None
		try:
			with open("/sys/class/power_supply/BAT0/status", 'r') as f:
				status = f.read().strip()
		except IOError as e:
			pass
		except ValueError as e:
			print("** Warning: Failed to read battery status: %s\n" % e)
			
		percentage = None
		try:
			with open("/sys/class/power_supply/BAT0/capacity", 'r') as f:
				percentage = int(f.read())
		except IOError as e:
			pass
		except ValueError as e:
			print("** Warning: Failed to read battery percentage: %s\n" % e)
			
		return {'status':status, 'percentage':percentage}


battery_info_default = VosBatteryInfo()
def battery_info_get_default():
	return battery_info_default



class VosBatteryIcon(Gtk.Image):
	__gtype_name__ = 'VosBatteryIcon'
	
	stopUpdates = False
	updateThread = None
	prevIconName = None
	battery = battery_info_get_default()
	
	def __init__(self):
		super().__init__()

		self.get_style_context().add_class("battery-icon")
		self.connect("destroy", self.on_destroy)
		self.set_from_icon_name("battery-full-charged-symbolic", Gtk.IconSize.MENU)
		self.battery.connect("update", self.update)
		
	def on_destroy(self, widget):
		self.stopUpdates = True
	
	def update(self, battery, percentage, status):
		iconName = "battery-"

		if percentage <= 10:
			iconName += "empty"
		elif percentage <= 35:
			iconName += "low"
		elif percentage <= 75:
			iconName += "good"
		else:
			iconName += "full"

		if status == "Full":
			iconName = "battery-full-charged-symbolic"
		elif status == "Charging":
			iconName += "-charging-symbolic";
		else:
			iconName += "-symbolic";			

		if self.prevIconName != iconName:
			self.prevIconName = iconName
			GLib.idle_add(self.set_from_icon_name, iconName, Gtk.IconSize.MENU)
			
			if percentage <= 10:
				self.get_style_context().add_class("battery-warning")
			else:
				self.get_style_context().remove_class("battery-warning")
from pteros_py import *

class Task:
	def pre_process(self):
		print "pre_process in sample2"
	def post_process(self,info):
		print "post_process in sample2"
	def process_frame(self,info):
		print "process_frame in sample2. Frame: ", info.valid_frame

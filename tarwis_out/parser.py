import xml.etree.ElementTree
from decimal import Decimal
import numpy as np
import matplotlib.pyplot as plt
from random import *
import glob, os

os.chdir("files")
for file in glob.glob("*.wiseml"):
    root = xml.etree.ElementTree.parse(file).getroot()
    trace = root.find('{http://wisebed.eu/ns/wiseml/1.0}trace')
    data = {}
    nodes = []
	# loop_count
    x1 = []
	# tarwis time
    x2 = []
	# time difference
    y = []
	#colors
    cs = []
	# 40 different colors
    colors = plt.cm.rainbow(np.linspace(0,1,40))
	
    for child in trace:
        if child.tag == '{http://wisebed.eu/ns/wiseml/1.0}timestamp':
            timestamp = (float) (Decimal(child.text))
        elif child.tag == '{http://wisebed.eu/ns/wiseml/1.0}node':
            valueSplit = (child.find('{http://wisebed.eu/ns/wiseml/1.0}data').text).split(":")
            if valueSplit[0] == 'LoopTime':
    			loop_count = (float) (Decimal(valueSplit[1]))
    			node_time = valueSplit[2]
    			node_id = child.attrib['id']
    			diff = ((float) (Decimal(node_time))) - timestamp
				
				
    			if not node_id in data:
    				data[node_id] = {}
    			data[node_id][loop_count] = diff
				
    			if not node_id in nodes:
    				nodes.append(node_id)
				
    			x1.append(loop_count)
    			x2.append(timestamp)
    			y.append(diff)
    			cs.append(colors[nodes.index(node_id)])
    
    #print data
    
    fig = plt.figure()
    figure = fig.add_subplot(111)
    figure.scatter(x1,y, color=cs)
	
    xmax = np.amax(x1);
    xmin = np.amin(x1);
	
    figure.plot([xmin,xmax],[np.mean(y)] * 2)
    figure.set_xlim([xmin,xmax])
    figure.set_ylim([np.amin(y),np.amax(y)])
    fig.savefig('../images/'+file+'_loopCount.png', dpi=72)
    plt.close()
	
    fig = plt.figure()
    figure = fig.add_subplot(111)
    figure.scatter(x2,y, color=cs)
	
    xmax = np.amax(x2);
    xmin = np.amin(x2);
	
    figure.plot([xmin,xmax],[np.mean(y)] * 2)
    figure.set_xlim([xmin,xmax])
    figure.set_ylim([np.amin(y),np.amax(y)])
    fig.savefig('../images/'+file+'_tarwisTime.png', dpi=72)
    plt.close()
  



        




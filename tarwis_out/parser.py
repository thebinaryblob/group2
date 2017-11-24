import xml.etree.ElementTree
from decimal import Decimal
import numpy as np
import matplotlib.pyplot as plt
from random import *

root = xml.etree.ElementTree.parse('test.wiseml').getroot()
trace = root.find('{http://wisebed.eu/ns/wiseml/1.0}trace')
data = {}
x = []
y = []

for child in trace:
    if child.tag == '{http://wisebed.eu/ns/wiseml/1.0}timestamp':
        timestamp = Decimal(child.text)
    elif child.tag == '{http://wisebed.eu/ns/wiseml/1.0}node':
        valueSplit = (child.find('{http://wisebed.eu/ns/wiseml/1.0}data').text).split(":")
        if valueSplit[0] == 'LoopTime':
			loop_count = (float) (Decimal(valueSplit[1]))
			node_time = valueSplit[2]
			node_id = child.attrib['id']
			diff = (float) (Decimal(node_time) - timestamp)
			if not node_id in data:
				data[node_id] = {}
			data[node_id][loop_count] = diff
			x.append(loop_count)
			y.append(diff)

print data

figure = plt.subplot(111)
figure.scatter(x,y)
figure.plot([0,200],[np.mean(y)] * 2)
figure.set_xlim([np.amin(x),np.amax(x)])
figure.set_ylim([np.amin(y),np.amax(y)])

plt.show()



        




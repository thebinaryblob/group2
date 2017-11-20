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
        string = (child.find('{http://wisebed.eu/ns/wiseml/1.0}data').text).split(":")[1]
        node_id = child.attrib['id']
        diff = (float) (Decimal(string) - timestamp)
        loop = randint(100,1500) # TODO add loop parsing
        if not node_id in data:
            data[node_id] = {}
        data[node_id][loop] = diff
        x.append(loop)
        y.append(diff)

print data

figure = plt.subplot(111)
figure.scatter(x,y)
figure.set_xlim([0,1500])
figure.set_ylim([np.amin(y),np.amax(y)])

plt.show()



        




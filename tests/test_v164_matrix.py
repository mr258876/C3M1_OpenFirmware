#!/usr/bin/env python3
"""Validate DNG tags & 67.5% visual color transform direction and D50 white."""
import json,struct,sys
from pathlib import Path
import numpy as np
b=Path(sys.argv[1]).read_bytes()
n=struct.unpack_from('<H',b,8)[0]
tags={}
for i in range(n):
    tag,kind,count,val=struct.unpack_from('<HHII',b,10+i*12)
    tags[tag]=(kind,count,val)
assert n==23 and 50964 in tags and tags[50778]==(3,1,23)
def mat(tag):
    typ,count,pos=tags[tag]
    assert typ==10 and count==9
    assert pos+9*8<=512
    return np.array([struct.unpack_from('<ii',b,pos+8*i)[0]/struct.unpack_from('<ii',b,pos+8*i)[1] for i in range(9)]).reshape(3,3)
color=mat(50721);forward=mat(50964)
# Independently confirm reference M -> experimental M67 and D50 PCS conversion.
reference=np.array([[1.6337699235873062,-.5928547264083479,-.04091519717895831],[-.3031413154104898,1.526695272574872,-.2235539571643821],[.09654436910177054,-.9691515703566853,1.8726072012549149]])
M=.325*np.eye(3)+.675*reference
srgb_to_xyz50=np.array([[.4360747,.3850649,.1430804],[.2225045,.7168786,.0606169],[.0139322,.0971045,.7141733]])
expected_forward=srgb_to_xyz50@M
neutral=np.array([1.0455465587044535,1.,.5332307692307692])
expected_color=np.linalg.inv(expected_forward@np.diag(1/neutral))
assert np.max(np.abs(color-expected_color))<.00001
assert np.max(np.abs(forward-expected_forward))<.00001
assert np.allclose(forward@np.ones(3),[.96422,1.,.82521],atol=2e-5)
assert np.allclose(color@np.array([.96422,1.,.82521]),neutral,atol=2e-5)
assert np.linalg.det(color)>.01
print('PASS DNG ColorMatrix1 XYZ->native and ForwardMatrix1 WB-native->D50 XYZ; 67.5%, D50 white')

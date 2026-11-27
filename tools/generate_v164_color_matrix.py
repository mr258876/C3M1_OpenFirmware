#!/usr/bin/env python3
"""Reproduce experimental DNG matrices. Requires NumPy. Not linked into MCU."""
import json
from pathlib import Path
import numpy as np
p=Path(__file__).parent/'C3M1_实验标定初始数据.json'
data=json.loads(p.read_text(encoding='utf-8'))
M=np.array(data['matrix_rgb_to_linear_srgb'],dtype=float)
n=np.array(data['whitebalance_neutral'],dtype=float)
A=0.325*np.eye(3)+0.675*M
# sRGB (D65) -> PCS XYZ D50, ICC-adapted matrix.
S=np.array([[.4360747,.3850649,.1430804],
            [.2225045,.7168786,.0606169],
            [.0139322,.0971045,.7141733]])
forward=S@A
color=np.linalg.inv(forward@np.diag(1/n))
print('DNG ColorMatrix1 XYZ_D50 -> native camera:')
print(np.rint(color*100000).astype(int))
print('DNG ForwardMatrix1 WB camera -> XYZ_D50:')
print(np.rint(forward*100000).astype(int))
print('White consistency: ',color@np.array([.96422,1.,.82521]),'expected',n)

#!/usr/bin/env python

"""
setup.py file for SWIG example
"""

from distutils.core import setup, Extension

#
# swig -python pyjel.i
# gcc -c pyjel_wrap.c -I../include -I/usr/local/Cellar/python@3.9/3.9.8/Frameworks/Python.framework/Versions/3.9/include/python3.9/
# python3 setup.py build_ext --inplace
#


#
# Issue: We need to pull in jel and libjpeg, although the latter
# should not be exposed.

pyjel_module = Extension('_pyjel',
                         sources=['py/pyjel_wrap.c',
                                  'libjel/jel.c',
                                  'libjel/ijel.c',
                                  'libjel/ijel-ecc.c',
                                  'libjel/jpeg-mem-dst.c',
                                  'libjel/jpeg-mem-src.c',
                                  'libjel/jpeg-stdio-dst.c',
                                  'libjel/jpeg-stdio-src.c',
                                  'rscode/rs.c',
                                  'rscode/berlekamp.c',
                                  'rscode/crcgen.c',
                                  'rscode/galois.c',
                                  ],
                         include_dirs=['include',
                                       'include/rscode',
                                       ],
                         libraries=['jpeg'],
                         )

setup (name = 'pyjel',
       version = '0.47',
       author      = "SWIG Docs",
       description = """Simple swig example from docs""",
       package_dir = {'': 'py'},
       ext_modules = [pyjel_module],
       py_modules = ["pyjel", "pywedge", "m4s"],
       )

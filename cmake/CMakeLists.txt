cmake_minimum_required(VERSION 3.0)
project(dclimplode)

find_package(pybind11 REQUIRED)
pybind11_add_module(dclimplode.dclimplode
  ../src/dclimplode.cpp
  ../src/blast/blast.c ../src/pklib/explode.c ../src/pklib/implode.c
)

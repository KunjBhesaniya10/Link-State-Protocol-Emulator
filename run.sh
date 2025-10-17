#!/bin/bash

# Run VN.cpp 6 times in different terminals 
for i in {1..6}
do
  gnome-terminal -- bash -c "g++ VN.cpp -o VN && ./VN 192.168.52.56 192.168.52.56; exec bash"
done
